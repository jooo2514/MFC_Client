#include "pch.h"
#include "framework.h"
#include "CanClient.h"
#include "CanClientDlg.h"
#include "afxdialogex.h"

#include <pylon/PylonIncludes.h>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <vector>
#include <ShlObj.h>  // SHCreateDirectoryEx 사용
#pragma comment(lib, "Shell32.lib")

#pragma comment(lib, "ws2_32.lib")

using namespace Pylon;
using namespace GenApi;
using namespace cv;

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

// ===================== 전역 =====================
namespace {
    CInstantCamera g_camera;
    CGrabResultPtr g_grab;
    CImageFormatConverter g_conv;
    CPylonImage g_pimg;
    UINT_PTR g_timerId = 0;

    // Mat을 Picture Control에 출력
    void DrawMatToCtrl(const Mat& bgr, CWnd* pCtrl)
    {
        if (!pCtrl || bgr.empty()) return;
        CClientDC dc(pCtrl);
        CRect rc; pCtrl->GetClientRect(&rc);

        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = bgr.cols;
        bmi.bmiHeader.biHeight = -bgr.rows;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 24;
        bmi.bmiHeader.biCompression = BI_RGB;
        bmi.bmiHeader.biSizeImage = bgr.cols * bgr.rows * 3;

        StretchDIBits(dc.GetSafeHdc(),
            0, 0, rc.Width(), rc.Height(),
            0, 0, bgr.cols, bgr.rows,
            bgr.data, &bmi, DIB_RGB_COLORS, SRCCOPY);
    }

    // Basler 카메라 초기 설정
    void ApplyCameraConfig(INodeMap& nm)
    {
        if (CEnumerationPtr pf = nm.GetNode("PixelFormat"); IsWritable(pf))
            pf->FromString("Mono8");

        if (CEnumerationPtr eauto = nm.GetNode("ExposureAuto"); IsWritable(eauto))
            eauto->FromString("Off");
        if (CEnumerationPtr gauto = nm.GetNode("GainAuto"); IsWritable(gauto))
            gauto->FromString("Off");

        if (CFloatPtr exp = nm.GetNode("ExposureTime"); IsWritable(exp))
            exp->SetValue(15000.0);
        if (CFloatPtr gain = nm.GetNode("Gain"); IsWritable(gain))
            gain->SetValue(2.0);
        if (CFloatPtr gamma = nm.GetNode("Gamma"); IsWritable(gamma))
            gamma->SetValue(1.0);

        if (CIntegerPtr w = nm.GetNode("Width"); IsWritable(w))
            w->SetValue(1952);
        if (CIntegerPtr h = nm.GetNode("Height"); IsWritable(h))
            h->SetValue(1232);
    }

    // TCP로 이미지 파일 전송
    bool SendImageToServer(const std::string& filePath, const std::string& serverIp, int port)
    {
        WSADATA wsa;
        SOCKET sock = INVALID_SOCKET;
        FILE* fp = nullptr;

        try {
            if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
                throw std::runtime_error("WSAStartup 실패");

            sock = socket(AF_INET, SOCK_STREAM, 0);
            if (sock == INVALID_SOCKET)
                throw std::runtime_error("소켓 생성 실패");

            sockaddr_in server = {};
            server.sin_family = AF_INET;
            server.sin_port = htons(port);
            inet_pton(AF_INET, serverIp.c_str(), &server.sin_addr);

            if (connect(sock, (sockaddr*)&server, sizeof(server)) < 0)
                throw std::runtime_error("서버 연결 실패");

            if (fopen_s(&fp, filePath.c_str(), "rb") != 0 || !fp)
                throw std::runtime_error("파일 열기 실패");

            fseek(fp, 0, SEEK_END);
            long fileSize = ftell(fp);
            fseek(fp, 0, SEEK_SET);

            // 파일 크기 전송
            send(sock, (char*)&fileSize, sizeof(fileSize), 0);

            // 파일 데이터 전송
            std::vector<char> buffer(4096);
            while (!feof(fp))
            {
                size_t bytes = fread(buffer.data(), 1, buffer.size(), fp);
                if (bytes > 0) send(sock, buffer.data(), (int)bytes, 0);
            }

            fclose(fp);
            fp = nullptr;

            // 서버 응답 수신
            char recvBuf[256] = { 0 };
            int len = recv(sock, recvBuf, sizeof(recvBuf) - 1, 0);
            if (len > 0)
            {
                recvBuf[len] = 0;
                CString msg(recvBuf);
                AfxMessageBox(msg);
            }
        }
        catch (const std::exception& e) {
            AfxMessageBox(CString("전송 실패: ") + CString(e.what()));
            if (fp) fclose(fp);
            if (sock != INVALID_SOCKET) closesocket(sock);
            WSACleanup();
            return false;
        }

        if (sock != INVALID_SOCKET) closesocket(sock);
        WSACleanup();
        return true;
    }
}

// ===================== CCanClientDlg =====================
CCanClientDlg::CCanClientDlg(CWnd* pParent /*=nullptr*/)
    : CDialogEx(IDD_CANCLIENT_DIALOG, pParent)
{
    m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
}

void CCanClientDlg::DoDataExchange(CDataExchange* pDX)
{
    CDialogEx::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CCanClientDlg, CDialogEx)
    ON_WM_PAINT()
    ON_WM_QUERYDRAGICON()
    ON_WM_TIMER()
    ON_WM_DESTROY()
    ON_BN_CLICKED(IDC_BTN_START, &CCanClientDlg::OnBnClickedBtnStart)
END_MESSAGE_MAP()

BOOL CCanClientDlg::OnInitDialog()
{
    CDialogEx::OnInitDialog();
    SetIcon(m_hIcon, TRUE);
    SetIcon(m_hIcon, FALSE);
    SetDlgItemText(IDC_STATIC_STATUS, _T("카메라 대기 중..."));
    return TRUE;
}

// ===================== 버튼: 시작/정지 + 촬영/전송 =====================
void CCanClientDlg::OnBnClickedBtnStart()
{
    if (!g_camera.IsOpen())
    {
        // ---- 카메라 시작 ----
        try {
            PylonInitialize();
            g_camera.Attach(CTlFactory::GetInstance().CreateFirstDevice());
            g_camera.Open();
            ApplyCameraConfig(g_camera.GetNodeMap());

            g_conv.OutputPixelFormat = PixelType_Mono8;
            g_camera.StartGrabbing(GrabStrategy_LatestImageOnly);

            g_timerId = SetTimer(1, 33, nullptr);
            SetDlgItemText(IDC_STATIC_STATUS, _T("카메라 실행 중... (다시 클릭 시 촬영)"));
        }
        catch (const GenericException& e) {
            CString msg(e.GetDescription());
            AfxMessageBox(msg);
        }
        return;
    }

    // ---- 현재 프레임 캡처 & 서버 전송 ----
    if (g_camera.IsGrabbing())
    {
        if (g_camera.RetrieveResult(500, g_grab, TimeoutHandling_Return) && g_grab->GrabSucceeded())
        {
            g_conv.Convert(g_pimg, g_grab);

            Mat frame((int)g_grab->GetHeight(), (int)g_grab->GetWidth(), CV_8UC1, (void*)g_pimg.GetBuffer());
            Mat bgr; cvtColor(frame, bgr, COLOR_GRAY2BGR);

            // JPG 저장
            CString dir = _T("C:\\CanClient\\captures");
            SHCreateDirectoryEx(NULL, dir, NULL);

            CString filename;
            CTime now = CTime::GetCurrentTime();
            filename.Format(_T("%s\\capture_%04d%02d%02d_%02d%02d%02d.jpg"),
                dir, now.GetYear(), now.GetMonth(), now.GetDay(),
                now.GetHour(), now.GetMinute(), now.GetSecond());

            // UTF-8 변환
            CT2A utf8File(filename, CP_UTF8);
            std::string path(utf8File.m_psz);

            if (!cv::imwrite(path, bgr)) {
                AfxMessageBox(_T("이미지 저장 실패"));
                return;
            }

            // 서버 전송
            SetDlgItemText(IDC_STATIC_STATUS, _T("서버로 이미지 전송 중..."));
            bool ok = SendImageToServer(path, "127.0.0.1", 9000);
            if (ok)
                SetDlgItemText(IDC_STATIC_STATUS, _T("서버 전송 완료 ✅"));
            else
                SetDlgItemText(IDC_STATIC_STATUS, _T("전송 실패 ❌"));
        }
    }
}

// ===================== OnTimer: 프레임 표시 =====================
void CCanClientDlg::OnTimer(UINT_PTR nIDEvent)
{
    if (nIDEvent == 1 && g_camera.IsGrabbing())
    {
        try
        {
            if (g_camera.RetrieveResult(100, g_grab, TimeoutHandling_Return))
            {
                if (g_grab->GrabSucceeded())
                {
                    g_conv.Convert(g_pimg, g_grab);
                    Mat gray((int)g_grab->GetHeight(), (int)g_grab->GetWidth(), CV_8UC1, (void*)g_pimg.GetBuffer());
                    Mat bgr; cvtColor(gray, bgr, COLOR_GRAY2BGR);
                    DrawMatToCtrl(bgr, GetDlgItem(IDC_CAM_VIEW));
                }
            }
        }
        catch (...) {}
    }

    CDialogEx::OnTimer(nIDEvent);
}

// ===================== 종료 처리 =====================
void CCanClientDlg::OnDestroy()
{
    CDialogEx::OnDestroy();
    if (g_timerId) { KillTimer(g_timerId); g_timerId = 0; }
    try { if (g_camera.IsGrabbing()) g_camera.StopGrabbing(); }
    catch (...) {}
    try { if (g_camera.IsOpen()) g_camera.Close(); }
    catch (...) {}
    try { PylonTerminate(); }
    catch (...) {}
}

// ===================== 기본 윈도우 처리 =====================
void CCanClientDlg::OnPaint()
{
    if (IsIconic())
    {
        CPaintDC dc(this);
        SendMessage(WM_ICONERASEBKGND, reinterpret_cast<WPARAM>(dc.GetSafeHdc()), 0);
        const int cx = GetSystemMetrics(SM_CXICON);
        const int cy = GetSystemMetrics(SM_CYICON);
        CRect rc; GetClientRect(&rc);
        const int x = (rc.Width() - cx + 1) / 2;
        const int y = (rc.Height() - cy + 1) / 2;
        dc.DrawIcon(x, y, m_hIcon);
    }
    else CDialogEx::OnPaint();
}

HCURSOR CCanClientDlg::OnQueryDragIcon()
{
    return static_cast<HCURSOR>(m_hIcon);
}
