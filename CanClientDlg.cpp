#include "pch.h"
#include "framework.h"
#include "CanClient.h"
#include "CanClientDlg.h"
#include "afxdialogex.h"

#include <fstream>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

using namespace Pylon;
using namespace cv;

// ===================== MFC 연결 =====================
void CCanClientDlg::DoDataExchange(CDataExchange* pDX)
{
    CDialogEx::DoDataExchange(pDX);
}

// ===================== 이미지 출력 =====================
void CCanClientDlg::DrawMatToCtrl(const Mat& img, CWnd* pWnd)
{
    if (!pWnd || img.empty()) return;
    CClientDC dc(pWnd);
    CRect rc; pWnd->GetClientRect(&rc);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = img.cols;
    bmi.bmiHeader.biHeight = -img.rows;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 24;
    bmi.bmiHeader.biCompression = BI_RGB;

    StretchDIBits(dc.GetSafeHdc(),
        0, 0, rc.Width(), rc.Height(),
        0, 0, img.cols, img.rows,
        img.data, &bmi, DIB_RGB_COLORS, SRCCOPY);
}

// ===================== TCP 전송 =====================
bool CCanClientDlg::SendImageToServer(const std::string& imgPath)
{
    // ---- 파일 읽기 ----
    std::ifstream file(imgPath, std::ios::binary | std::ios::ate);
    if (!file) {
        OutputDebugString(L"[ERROR] 이미지 파일 열기 실패\n");
        return false;
    }
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<char> buffer(size);
    if (!file.read(buffer.data(), size)) {
        OutputDebugString(L"[ERROR] 파일 읽기 실패\n");
        return false;
    }

    // ---- 소켓 초기화 ----
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        OutputDebugString(L"[ERROR] WSAStartup 실패\n");
        return false;
    }

    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        WSACleanup();
        OutputDebugString(L"[ERROR] 소켓 생성 실패\n");
        return false;
    }

    sockaddr_in serverAddr = {};
    serverAddr.sin_family = AF_INET;
    //serverAddr.sin_port = htons(9000);
    //inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr); // 로컬 테스트용
    serverAddr.sin_port = htons(9000);
    inet_pton(AF_INET, "10.10.21.121", &serverAddr.sin_addr); // 로컬 테스트용

    if (connect(sock, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        OutputDebugString(L"[ERROR] 서버 연결 실패\n");
        closesocket(sock);
        WSACleanup();
        return false;
    }

    // ---- 1️⃣ 이미지 크기 전송 (네트워크 바이트 순서로) ----
    int fileSize = static_cast<int>(size);
    int netSize = htonl(fileSize); // 핵심!
    if (send(sock, (char*)&netSize, sizeof(netSize), 0) != sizeof(netSize)) {
        OutputDebugString(L"[ERROR] 길이 전송 실패\n");
        closesocket(sock);
        WSACleanup();
        return false;
    }

    // ---- 2️⃣ 이미지 데이터 전송 ----
    int totalSent = 0;
    while (totalSent < fileSize) {
        int sent = send(sock, buffer.data() + totalSent, fileSize - totalSent, 0);
        if (sent <= 0) {
            OutputDebugString(L"[ERROR] 데이터 전송 실패\n");
            closesocket(sock);
            WSACleanup();
            return false;
        }
        totalSent += sent;
    }

    OutputDebugString(L"[INFO] 이미지 전송 완료\n");

    closesocket(sock);
    WSACleanup();
    return true;
}

// ===================== 메시지 맵 =====================
BEGIN_MESSAGE_MAP(CCanClientDlg, CDialogEx)
    ON_BN_CLICKED(IDC_BTN_START, &CCanClientDlg::OnBnClickedBtnStart)
    ON_WM_DESTROY()
    ON_WM_TIMER()
END_MESSAGE_MAP()

// ===================== 생성자 =====================
CCanClientDlg::CCanClientDlg(CWnd* pParent)
    : CDialogEx(IDD_CANCLIENT_DIALOG, pParent)
{
    m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
}

// ===================== 초기화 =====================
BOOL CCanClientDlg::OnInitDialog()
{
    CDialogEx::OnInitDialog();
    SetIcon(m_hIcon, TRUE);
    SetIcon(m_hIcon, FALSE);
    SetDlgItemText(IDC_STATIC_STATUS, _T("카메라 연결 중..."));

    try {
        PylonInitialize();
        CTlFactory& factory = CTlFactory::GetInstance();
        DeviceInfoList_t devices;
        if (factory.EnumerateDevices(devices) < 2) {
            AfxMessageBox(L"Basler 카메라 2대를 모두 연결해야 합니다.");
            return TRUE;
        }

        m_camTop.Attach(factory.CreateDevice(devices[0]));
        m_camFront.Attach(factory.CreateDevice(devices[1]));

        m_camTop.Open();
        m_camFront.Open();

        m_camTop.StartGrabbing(GrabStrategy_LatestImageOnly);
        m_camFront.StartGrabbing(GrabStrategy_LatestImageOnly);

        m_timerId = SetTimer(1, 33, nullptr);
        SetDlgItemText(IDC_STATIC_STATUS, _T("카메라 2대 연결 완료"));
    }
    catch (const GenericException& e) {
        CString msg(e.GetDescription());
        AfxMessageBox(msg);
    }
    return TRUE;
}

// ===================== 타이머 (미리보기) =====================
void CCanClientDlg::OnTimer(UINT_PTR nIDEvent)
{
    if (nIDEvent == 1)
    {
        try {
            CGrabResultPtr grabTop, grabFront;

            if (m_camTop.IsGrabbing() &&
                m_camTop.RetrieveResult(50, grabTop, TimeoutHandling_Return) &&
                grabTop->GrabSucceeded())
            {
                m_converter.OutputPixelFormat = PixelType_BGR8packed;
                m_converter.Convert(m_pylonImage, grabTop);
                Mat img((int)grabTop->GetHeight(), (int)grabTop->GetWidth(),
                    CV_8UC3, (void*)m_pylonImage.GetBuffer());
                DrawMatToCtrl(img, GetDlgItem(IDC_CAM_TOP));
            }

            if (m_camFront.IsGrabbing() &&
                m_camFront.RetrieveResult(50, grabFront, TimeoutHandling_Return) &&
                grabFront->GrabSucceeded())
            {
                m_converter.OutputPixelFormat = PixelType_BGR8packed;
                m_converter.Convert(m_pylonImage, grabFront);
                Mat img((int)grabFront->GetHeight(), (int)grabFront->GetWidth(),
                    CV_8UC3, (void*)m_pylonImage.GetBuffer());
                DrawMatToCtrl(img, GetDlgItem(IDC_CAM_FRONT));
            }
        }
        catch (...) {
            OutputDebugString(L"[Basler] RetrieveResult error\n");
        }
    }
    CDialogEx::OnTimer(nIDEvent);
}

// ===================== 촬영 및 전송 =====================
void CCanClientDlg::OnBnClickedBtnStart()
{
    GetDlgItem(IDC_BTN_START)->EnableWindow(FALSE);
    SetDlgItemText(IDC_STATIC_STATUS, _T("이미지 캡처 중..."));

    try {
        CString folder = L"C:\\CanClient\\captures";
        CreateDirectory(folder, NULL);

        if (!m_camTop.IsOpen()) m_camTop.Open();
        if (!m_camFront.IsOpen()) m_camFront.Open();

        if (!m_camTop.IsGrabbing())
            m_camTop.StartGrabbing(GrabStrategy_LatestImageOnly);
        if (!m_camFront.IsGrabbing())
            m_camFront.StartGrabbing(GrabStrategy_LatestImageOnly);

        Sleep(100);

        CGrabResultPtr grabTop, grabFront;

        // ---- 상단 ----
        if (m_camTop.RetrieveResult(800, grabTop, TimeoutHandling_Return) && grabTop->GrabSucceeded()) {
            m_converter.OutputPixelFormat = PixelType_BGR8packed;
            CPylonImage imgTop;
            m_converter.Convert(imgTop, grabTop);
            Mat topMat((int)grabTop->GetHeight(), (int)grabTop->GetWidth(), CV_8UC3, (void*)imgTop.GetBuffer());

            std::string topPath = "C:\\CanClient\\captures\\capture_" +
                std::to_string(time(NULL)) + "_top.jpg";

            if (imwrite(topPath, topMat))
                SendImageToServer(topPath);
        }

        // ---- 정면 ----
        if (m_camFront.RetrieveResult(800, grabFront, TimeoutHandling_Return) && grabFront->GrabSucceeded()) {
            m_converter.OutputPixelFormat = PixelType_BGR8packed;
            CPylonImage imgFront;
            m_converter.Convert(imgFront, grabFront);
            Mat frontMat((int)grabFront->GetHeight(), (int)grabFront->GetWidth(), CV_8UC3, (void*)imgFront.GetBuffer());

            std::string frontPath = "C:\\CanClient\\captures\\capture_" +
                std::to_string(time(NULL)) + "_front.jpg";

            if (imwrite(frontPath, frontMat))
                SendImageToServer(frontPath);
        }

        SetDlgItemText(IDC_STATIC_STATUS, _T("상단/정면 촬영 및 전송 완료 ✅"));
    }
    catch (const GenericException& e) {
        CString msg(e.GetDescription());
        AfxMessageBox(msg);
    }

    GetDlgItem(IDC_BTN_START)->EnableWindow(TRUE);
}

// ===================== 종료 =====================
void CCanClientDlg::OnDestroy()
{
    CDialogEx::OnDestroy();
    if (m_timerId) { KillTimer(m_timerId); m_timerId = 0; }

    try {
        if (m_camTop.IsGrabbing()) m_camTop.StopGrabbing();
        if (m_camTop.IsOpen()) m_camTop.Close();
        if (m_camFront.IsGrabbing()) m_camFront.StopGrabbing();
        if (m_camFront.IsOpen()) m_camFront.Close();
        PylonTerminate();
    }
    catch (...) {}
}
