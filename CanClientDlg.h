#pragma once

#include <opencv2/opencv.hpp>  // OpenCV 헤더 포함

// CCanClientDlg 대화 상자
class CCanClientDlg : public CDialogEx
{
public:
    CCanClientDlg(CWnd* pParent = nullptr); // 생성자

#ifdef AFX_DESIGN_TIME
    enum { IDD = IDD_CANCLIENT_DIALOG };
#endif

protected:
    virtual void DoDataExchange(CDataExchange* pDX); // DDX/DDV 지원

    // 구현
protected:
    HICON m_hIcon;  // ✅ 아이콘 핸들 (기본 MFC 코드에서 빠졌던 부분)
    DECLARE_MESSAGE_MAP()

    // 사용자 함수
protected:
    afx_msg BOOL OnInitDialog();
    afx_msg void OnPaint();
    afx_msg HCURSOR OnQueryDragIcon();
    afx_msg void OnBnClickedBtnStart();
    afx_msg void OnTimer(UINT_PTR nIDEvent);
    afx_msg void OnDestroy();

private:
    cv::VideoCapture cap; // 카메라 객체
    cv::Mat frame;        // 영상 프레임
    UINT_PTR timerID = 0; // 타이머 ID
};
