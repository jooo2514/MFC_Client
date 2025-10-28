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
    virtual BOOL OnInitDialog();
    afx_msg void OnDestroy();
    afx_msg void OnBnClickedBtnStart();
    afx_msg void OnTimer(UINT_PTR nIDEvent);
    DECLARE_MESSAGE_MAP()

private:
    HICON m_hIcon;

    // 카메라 2대
    CInstantCamera m_camTop;
    CInstantCamera m_camFront;

    // 변환기/결과 이미지
    CImageFormatConverter m_converter;
    CPylonImage m_pylonImage;

    UINT_PTR m_timerId = 0;

    // 헬퍼 함수
    void DrawMatToCtrl(const cv::Mat& img, CWnd* pWnd);
    bool SendImageToServer(const std::string& imgPath);
};
