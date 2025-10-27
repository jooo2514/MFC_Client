#include "pch.h"
#include "framework.h"
#include "CanClient.h"
#include "CanClientDlg.h"
#include "afxdialogex.h"

// ---- OpenCV ----
#pragma warning(push, 0)
#include <opencv2/opencv.hpp>
#pragma warning(pop)
using namespace cv;

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

// 메시지 맵
BEGIN_MESSAGE_MAP(CCanClientDlg, CDialogEx)
    ON_BN_CLICKED(IDC_BTN_START, &CCanClientDlg::OnBnClickedBtnStart)
    ON_WM_TIMER()
    ON_WM_DESTROY()
    ON_WM_PAINT()
    ON_WM_QUERYDRAGICON()
END_MESSAGE_MAP()

// 생성자
CCanClientDlg::CCanClientDlg(CWnd* pParent /*=nullptr*/)
    : CDialogEx(IDD_CANCLIENT_DIALOG, pParent)
{
    // 아이콘 불러오기 (이 부분이 기존에 누락됨)
    m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
}

void CCanClientDlg::DoDataExchange(CDataExchange* pDX)
{
    CDialogEx::DoDataExchange(pDX);
}

// 초기화
BOOL CCanClientDlg::OnInitDialog()
{
    CDialogEx::OnInitDialog();

    // 시스템 메뉴에 "정보..." 메뉴 항목 추가
    CMenu* pSysMenu = GetSystemMenu(FALSE);
    if (pSysMenu != nullptr)
    {
        CString strAboutMenu;
        strAboutMenu.LoadString(IDS_ABOUTBOX);
        if (!strAboutMenu.IsEmpty())
        {
            pSysMenu->AppendMenu(MF_SEPARATOR);
            pSysMenu->AppendMenu(MF_STRING, IDM_ABOUTBOX, strAboutMenu);
        }
    }

    // 아이콘 설정
    SetIcon(m_hIcon, TRUE);
    SetIcon(m_hIcon, FALSE);

    SetDlgItemText(IDC_STATIC_STATUS, _T("상태: 준비됨"));
    return TRUE;
}

// 아이콘 그리기
void CCanClientDlg::OnPaint()
{
    if (IsIconic())
    {
        CPaintDC dc(this);
        SendMessage(WM_ICONERASEBKGND, reinterpret_cast<WPARAM>(dc.GetSafeHdc()), 0);

        int cxIcon = GetSystemMetrics(SM_CXICON);
        int cyIcon = GetSystemMetrics(SM_CYICON);
        CRect rect;
        GetClientRect(&rect);
        int x = (rect.Width() - cxIcon + 1) / 2;
        int y = (rect.Height() - cyIcon + 1) / 2;

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

// =================== 카메라 제어 ===================

void CCanClientDlg::OnBnClickedBtnStart()
{
    if (cap.isOpened()) {
        KillTimer(timerID);
        cap.release();
        SetDlgItemText(IDC_STATIC_STATUS, _T("카메라 정지됨"));
        return;
    }

    cap.open(0); // 0번 카메라 열기
    if (!cap.isOpened()) {
        AfxMessageBox(_T("카메라를 열 수 없습니다."));
        return;
    }

    // 해상도 지정 (선택)
    cap.set(CAP_PROP_FRAME_WIDTH, 1280);
    cap.set(CAP_PROP_FRAME_HEIGHT, 720);

    SetDlgItemText(IDC_STATIC_STATUS, _T("카메라 실행 중... 다시 클릭 시 정지"));
    timerID = SetTimer(1, 33, nullptr); // 30fps
}

void CCanClientDlg::OnTimer(UINT_PTR nIDEvent)
{
    if (nIDEvent == 1 && cap.isOpened())
    {
        cap >> frame;
        if (frame.empty()) return;

        // BGR → RGB 변환
        cvtColor(frame, frame, COLOR_BGR2RGB);

        // ---- 안전하게 Mat 복사 ----
        if (!frame.isContinuous())
            frame = frame.clone();

        // Picture Control 찾기
        CWnd* pWnd = GetDlgItem(IDC_CAM_VIEW);
        if (!pWnd) return;

        CRect rect;
        pWnd->GetClientRect(&rect);

        // ---- 안전한 복사 방식 ----
        CImage img;
        img.Create(frame.cols, frame.rows, 24);

        // OpenCV Mat → CImage 복사 (한 줄씩)
        for (int y = 0; y < frame.rows; ++y)
        {
            uchar* src = frame.ptr(y);
            uchar* dst = (uchar*)img.GetBits() + y * img.GetPitch();
            memcpy(dst, src, frame.cols * 3);
        }

        // ---- Picture Control에 그리기 ----
        CClientDC dc(pWnd);
        img.Draw(dc, 0, 0, rect.Width(), rect.Height(),
            0, 0, frame.cols, frame.rows);
    }

    CDialogEx::OnTimer(nIDEvent);
}


void CCanClientDlg::OnDestroy()
{
    CDialogEx::OnDestroy();
    if (cap.isOpened()) cap.release();
    if (timerID != 0) KillTimer(timerID);
}
