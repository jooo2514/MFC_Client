#include "pch.h"
#include "framework.h"
#include "CanClient.h"
#include "CanClientDlg.h"
#include "afxdialogex.h"

#include <pylon/PylonIncludes.h>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

using namespace Pylon;
using namespace GenApi;
using namespace cv;

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

// ===================== 전역(translation unit local) =====================
namespace {
    CInstantCamera      g_camera;
    CGrabResultPtr      g_grab;
    CImageFormatConverter g_conv;
    CPylonImage         g_pimg;
    UINT_PTR            g_timerId = 0;

    // Mat을 Picture Control(IDC_CAM_VIEW)에 그리기 (24bpp BGR)
    void DrawMatToCtrl(const Mat& bgr, CWnd* pCtrl)
    {
        if (!pCtrl || bgr.empty()) return;
        CClientDC dc(pCtrl);
        CRect rc; pCtrl->GetClientRect(&rc);

        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = bgr.cols;
        bmi.bmiHeader.biHeight = -bgr.rows; // 상단부터
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 24;
        bmi.bmiHeader.biCompression = BI_RGB;
        bmi.bmiHeader.biSizeImage = bgr.cols * bgr.rows * 3;

        StretchDIBits(dc.GetSafeHdc(),
            0, 0, rc.Width(), rc.Height(),
            0, 0, bgr.cols, bgr.rows,
            bgr.data, &bmi, DIB_RGB_COLORS, SRCCOPY);
    }

    // 카메라 기본 설정(pylon 5.x 호환: GenApi 노드 접근)
    void ApplyCameraConfig(INodeMap& nm)
    {
        // PixelFormat = Mono8 (흑백 센서)
        if (CEnumerationPtr pf = nm.GetNode("PixelFormat"); IsWritable(pf))
            pf->FromString("Mono8");

        // 자동노출/게인 OFF
        if (CEnumerationPtr eauto = nm.GetNode("ExposureAuto"); IsWritable(eauto))
            eauto->FromString("Off");
        if (CEnumerationPtr gauto = nm.GetNode("GainAuto"); IsWritable(gauto))
            gauto->FromString("Off");

        // 노출/게인/감마(실내 기준 안전값)
        if (CFloatPtr exp = nm.GetNode("ExposureTime"); IsWritable(exp))
            exp->SetValue(80000.0); // 80ms
        if (CFloatPtr gain = nm.GetNode("Gain"); IsWritable(gain))
            gain->SetValue(6.0);    // 6 dB
        if (CFloatPtr gamma = nm.GetNode("Gamma"); IsWritable(gamma))
            gamma->SetValue(1.1);

        // 해상도 최대
        if (CIntegerPtr w = nm.GetNode("Width"); IsWritable(w))   w->SetValue(w->GetMax());
        if (CIntegerPtr h = nm.GetNode("Height"); IsWritable(h))  h->SetValue(h->GetMax());
    }

    // pylon Viewer의 UserSet을 불러오되(가능하면), 없으면 수동설정
    void LoadUserSetOrFallback(INodeMap& nm)
    {
        try {
            CEnumerationPtr sel = nm.GetNode("UserSetSelector");
            CCommandPtr     load = nm.GetNode("UserSetLoad");
            if (IsWritable(sel))  sel->FromString("AutoFunctions"); // 뷰어 덤프에 있었던 값
            if (IsWritable(load)) load->Execute();
            OutputDebugString(L"[Basler] UserSet AutoFunctions loaded\n");
        }
        catch (...) {
            OutputDebugString(L"[Basler] UserSet not available -> apply manual config\n");
            ApplyCameraConfig(nm);
        }
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

// ===================== OnInitDialog =====================
BOOL CCanClientDlg::OnInitDialog()
{
    CDialogEx::OnInitDialog();
    SetIcon(m_hIcon, TRUE);
    SetIcon(m_hIcon, FALSE);
    SetDlgItemText(IDC_STATIC_STATUS, _T("카메라 대기 중..."));
    return TRUE;
}

// ===================== 버튼: 시작/정지 =====================
void CCanClientDlg::OnBnClickedBtnStart()
{
    if (g_camera.IsGrabbing())
    {
        // ======= 카메라 정지 =======
        try { g_camera.StopGrabbing(); }
        catch (...) {}
        try { if (g_camera.IsOpen()) g_camera.Close(); }
        catch (...) {}
        try { PylonTerminate(); }
        catch (...) {}
        if (g_timerId) { KillTimer(g_timerId); g_timerId = 0; }
        SetDlgItemText(IDC_STATIC_STATUS, _T("카메라 정지됨"));
        return;
    }

    // ======= 카메라 시작 =======
    try {
        PylonInitialize();
        g_camera.Attach(CTlFactory::GetInstance().CreateFirstDevice());
        g_camera.Open();

        INodeMap& nm = g_camera.GetNodeMap();

        // ---------- 1. 기본 포맷 설정 ----------
        if (CEnumerationPtr pf = nm.GetNode("PixelFormat"); IsWritable(pf))
            pf->FromString("Mono8");

        // ---------- 2. 자동 모드 OFF ----------
        if (CEnumerationPtr eauto = nm.GetNode("ExposureAuto"); IsWritable(eauto))
            eauto->FromString("Off");
        if (CEnumerationPtr gauto = nm.GetNode("GainAuto"); IsWritable(gauto))
            gauto->FromString("Off");

        // ---------- 3. 노출 / 게인 / 감마 수동 설정 ----------
        if (CFloatPtr exp = nm.GetNode("ExposureTime"); IsWritable(exp))
            exp->SetValue(150000.0); // 120,000 µs = 0.12초 (실내 밝기 기준)
        if (CFloatPtr gain = nm.GetNode("Gain"); IsWritable(gain))
            gain->SetValue(8.0); // 밝기 보정
        if (CFloatPtr gamma = nm.GetNode("Gamma"); IsWritable(gamma))
            gamma->SetValue(1.4); // 명암 보정

        // ---------- 4. LUT (밝기 보정) ----------
        if (CBooleanPtr lutEnable = nm.GetNode("LUTEnable"); IsWritable(lutEnable))
            lutEnable->SetValue(true);

        // ---------- 5. PGI (노이즈 감소 + 샤프닝) ----------
        if (CEnumerationPtr pgiMode = nm.GetNode("PgiMode"); IsWritable(pgiMode))
            pgiMode->FromString("On");

        // ---------- 6. 해상도 최대값 ----------
        if (CIntegerPtr w = nm.GetNode("Width"); IsWritable(w)) w->SetValue(w->GetMax());
        if (CIntegerPtr h = nm.GetNode("Height"); IsWritable(h)) h->SetValue(h->GetMax());

        // ---------- 7. 출력 픽셀 포맷 변환기 ----------
        g_conv.OutputPixelFormat = PixelType_Mono8;

        // ---------- 8. 프레임 수신 시작 ----------
        g_camera.StartGrabbing(GrabStrategy_LatestImageOnly);
        g_timerId = SetTimer(1, 33, nullptr); // 약 30fps

        SetDlgItemText(IDC_STATIC_STATUS, _T("Basler 카메라 실행 중... (다시 클릭 시 정지)"));
        OutputDebugString(L"[Basler] Camera started successfully.\n");
    }
    catch (const GenericException& e)
    {
        CString msg(e.GetDescription());
        AfxMessageBox(msg);

        // 안전 종료
        try { if (g_camera.IsGrabbing()) g_camera.StopGrabbing(); }
        catch (...) {}
        try { if (g_camera.IsOpen()) g_camera.Close(); }
        catch (...) {}
        try { PylonTerminate(); }
        catch (...) {}
        if (g_timerId) { KillTimer(g_timerId); g_timerId = 0; }

        SetDlgItemText(IDC_STATIC_STATUS, _T("카메라 초기화 실패"));
    }
}


// ===================== OnTimer: 프레임 수신/표시 =====================
void CCanClientDlg::OnTimer(UINT_PTR nIDEvent)
{
    if (nIDEvent == 1 && g_camera.IsGrabbing())
    {
        try
        {
            if (g_camera.RetrieveResult(500, g_grab, TimeoutHandling_Return))
            {
                if (g_grab->GrabSucceeded())
                {
                    // ====== Basler → OpenCV Mat 변환 ======
                    g_conv.Convert(g_pimg, g_grab);
                    cv::Mat gray(
                        (int)g_grab->GetHeight(),
                        (int)g_grab->GetWidth(),
                        CV_8UC1,
                        (void*)g_pimg.GetBuffer()
                    );

                    // ====== 밝기/감마 보정 ======
                    cv::Mat bright;
                    gray.convertTo(bright, CV_8U, 1.2, 10); // 대비↑, 밝기↑

                    // 감마 보정 (감마 < 1 → 밝게, >1 → 어둡게)
                    cv::Mat gammaCorrected;
                    bright.convertTo(bright, CV_32F, 1.0 / 255.0);
                    cv::pow(bright, 0.8, gammaCorrected); // 감마 0.8로 약간 밝게
                    gammaCorrected.convertTo(gammaCorrected, CV_8U, 255.0);

                    // ====== 부드럽게 (노이즈 완화) ======
                    cv::Mat smooth;
                    cv::bilateralFilter(gammaCorrected, smooth, 5, 50, 50);

                    // ====== BGR 변환 후 표시 ======
                    cv::Mat bgr;
                    cv::cvtColor(smooth, bgr, cv::COLOR_GRAY2BGR);
                    DrawMatToCtrl(bgr, GetDlgItem(IDC_CAM_VIEW));
                }
                else
                {
                    OutputDebugString(L"[Basler] Grab failed\n");
                }
            }
        }
        catch (const GenericException& e)
        {
            CString msg(e.GetDescription());
            OutputDebugString(msg + L"\n");
        }
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
    try { if (g_camera.IsOpen())     g_camera.Close(); }
    catch (...) {}
    try { PylonTerminate(); }
    catch (...) {}
}

// ===================== OnPaint / OnQueryDragIcon =====================
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
    else
    {
        CDialogEx::OnPaint();
    }
}

HCURSOR CCanClientDlg::OnQueryDragIcon()
{
    return static_cast<HCURSOR>(m_hIcon);
}
