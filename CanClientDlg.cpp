#include "pch.h"
#include "framework.h"
#include "CanClient.h"
#include "CanClientDlg.h"
#include "afxdialogex.h"

#pragma warning(push, 0)
#include <pylon/PylonIncludes.h>
#include <opencv2/opencv.hpp>
#pragma warning(pop)

using namespace Pylon;
using namespace cv;

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

BEGIN_MESSAGE_MAP(CCanClientDlg, CDialogEx)
    ON_BN_CLICKED(IDC_BTN_START, &CCanClientDlg::OnBnClickedBtnStart)
    ON_WM_TIMER()
    ON_WM_DESTROY()
    ON_WM_PAINT()
    ON_WM_QUERYDRAGICON()
END_MESSAGE_MAP()

// ---- 생성자 ----
CCanClientDlg::CCanClientDlg(CWnd* pParent /*=nullptr*/)
    : CDialogEx(IDD_CANCLIENT_DIALOG, pParent)
{
    m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
}

void CCanClientDlg::DoDataExchange(CDataExchange* pDX)
{
    CDialogEx::DoDataExchange(pDX);
}

// ---- 초기화 ----
BOOL CCanClientDlg::OnInitDialog()
{
    CDialogEx::OnInitDialog();
    SetIcon(m_hIcon, TRUE);
    SetIcon(m_hIcon, FALSE);
    SetDlgItemText(IDC_STATIC_STATUS, _T("상태: 준비됨"));
    return TRUE;
}

// ---- 아이콘 그리기 ----
void CCanClientDlg::OnPaint()
{
    if (IsIconic())
    {
        CPaintDC dc(this);
        SendMessage(WM_ICONERASEBKGND, reinterpret_cast<WPARAM>(dc.GetSafeHdc()), 0);
        int cxIcon = GetSystemMetrics(SM_CXICON);
        int cyIcon = GetSystemMetrics(SM_CYICON);
        CRect rect; GetClientRect(&rect);
        int x = (rect.Width() - cxIcon + 1) / 2;
        int y = (rect.Height() - cyIcon + 1) / 2;
        dc.DrawIcon(x, y, m_hIcon);
    }
    else
        CDialogEx::OnPaint();
}

HCURSOR CCanClientDlg::OnQueryDragIcon()
{
    return static_cast<HCURSOR>(m_hIcon);
}

// ========================================================
//  [촬영 시작] 버튼 클릭
// ========================================================
void CCanClientDlg::OnBnClickedBtnStart()
{
    if (m_isCameraRunning)
    {
        KillTimer(m_timerID);
        if (m_camera.IsGrabbing()) m_camera.StopGrabbing();
        if (m_camera.IsOpen()) m_camera.Close();
        PylonTerminate();

        m_isCameraRunning = false;
        SetDlgItemText(IDC_STATIC_STATUS, _T("카메라 정지됨"));
        return;
    }

    try
    {
        PylonInitialize();

        // 연결된 첫 번째 Basler 카메라 사용
        m_camera.Attach(CTlFactory::GetInstance().CreateFirstDevice());
       
        m_camera.Open();
        // 해상도 최대치로 설정
        m_camera.Open();
        GenApi::INodeMap& nodemap = m_camera.GetNodeMap();

        // === Pixel Format 컬러로 ===
        GenApi::CEnumerationPtr pixelFormat = nodemap.GetNode("PixelFormat");
        if (IsWritable(pixelFormat)) {
            if (GenApi::IsAvailable(pixelFormat->GetEntryByName("BGR8")))
                pixelFormat->FromString("BGR8");
            else if (GenApi::IsAvailable(pixelFormat->GetEntryByName("BayerRG8")))
                pixelFormat->FromString("BayerRG8");
        }

        // === 자동 모드 끄고 수동 밝기 설정 ===
        auto setEnum = [&](const char* name, const char* value) {
            GenApi::CEnumerationPtr node = nodemap.GetNode(name);
            if (IsWritable(node)) node->FromString(value);
            };
        auto setFloat = [&](const char* name, double value) {
            GenApi::CFloatPtr node = nodemap.GetNode(name);
            if (IsWritable(node)) node->SetValue(value);
            };

        setEnum("ExposureAuto", "Off");
        setEnum("GainAuto", "Off");
        setFloat("ExposureTime", 30000.0); // 밝기 개선
        setFloat("Gain", 3.0);
        setFloat("Gamma", 1.0);




        m_camera.StartGrabbing(GrabStrategy_LatestImageOnly);

        m_converter.OutputPixelFormat = PixelType_BGR8packed;

        m_isCameraRunning = true;
        SetDlgItemText(IDC_STATIC_STATUS, _T("Basler 카메라 실행 중... (다시 클릭 시 정지)"));
        m_timerID = SetTimer(1, 33, nullptr); // 30fps
    }
    catch (const GenericException& e)
    {
        CString msg(e.GetDescription());
        AfxMessageBox(_T("Basler 카메라 연결 실패:\n") + msg);
        PylonTerminate();
    }
}

// ========================================================
//  타이머 이벤트 (33ms마다 영상 갱신)
// ========================================================
void CCanClientDlg::OnTimer(UINT_PTR nIDEvent)
{
    if (nIDEvent == 1 && m_isCameraRunning && m_camera.IsGrabbing())
    {
        try
        {
            m_camera.RetrieveResult(5000, m_ptrGrabResult, TimeoutHandling_ThrowException);

            if (m_ptrGrabResult->GrabSucceeded())
            {
                // Pylon → OpenCV 변환
                m_converter.Convert(m_pylonImage, m_ptrGrabResult);
                m_frame = Mat(m_ptrGrabResult->GetHeight(),
                    m_ptrGrabResult->GetWidth(),
                    CV_8UC3,
                    (uint8_t*)m_pylonImage.GetBuffer());

                // Picture Control에 표시
                CWnd* pWnd = GetDlgItem(IDC_CAM_VIEW);
                if (!pWnd) return;
                CRect rect; pWnd->GetClientRect(&rect);

                cvtColor(m_frame, m_frame, COLOR_BGR2RGB);

                CImage img;
                img.Create(m_frame.cols, m_frame.rows, 24);
                for (int y = 0; y < m_frame.rows; ++y)
                {
                    memcpy((BYTE*)img.GetBits() + y * img.GetPitch(),
                        m_frame.ptr(y), m_frame.cols * 3);
                }

                CClientDC dc(pWnd);
                img.Draw(dc, 0, 0, rect.Width(), rect.Height(),
                    0, 0, m_frame.cols, m_frame.rows);
            }
        }
        catch (const GenericException& e)
        {
            CString msg(e.GetDescription());
            AfxMessageBox(_T("프레임 수신 실패:\n") + msg);
        }
    }

    CDialogEx::OnTimer(nIDEvent);
}

// ========================================================
//  종료 시 정리
// ========================================================
void CCanClientDlg::OnDestroy()
{
    CDialogEx::OnDestroy();

    if (m_isCameraRunning)
    {
        if (m_camera.IsGrabbing()) m_camera.StopGrabbing();
        if (m_camera.IsOpen()) m_camera.Close();
        PylonTerminate();
        m_isCameraRunning = false;
    }

    if (m_timerID != 0) KillTimer(m_timerID);
}
