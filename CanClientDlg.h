#pragma once
#include <pylon/PylonIncludes.h>
#include <opencv2/opencv.hpp>

using namespace Pylon;
using namespace cv;

class CCanClientDlg : public CDialogEx
{
public:
    CCanClientDlg(CWnd* pParent = nullptr);

#ifdef AFX_DESIGN_TIME
    enum { IDD = IDD_CANCLIENT_DIALOG };
#endif

protected:
    virtual void DoDataExchange(CDataExchange* pDX);

protected:
    HICON m_hIcon;
    DECLARE_MESSAGE_MAP()

protected:
    afx_msg BOOL OnInitDialog();
    afx_msg void OnPaint();
    afx_msg HCURSOR OnQueryDragIcon();
    afx_msg void OnBnClickedBtnStart();
    afx_msg void OnTimer(UINT_PTR nIDEvent);
    afx_msg void OnDestroy();

private:
    // ---- Basler 관련 멤버 ----
    CInstantCamera m_camera;
    CGrabResultPtr m_ptrGrabResult;
    CImageFormatConverter m_converter;
    CPylonImage m_pylonImage;

    // ---- OpenCV ----
    cv::Mat m_frame;

    UINT_PTR m_timerID = 0;
    bool m_isCameraRunning = false;
};
