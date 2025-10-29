#pragma once
#include <pylon/PylonIncludes.h>
#include <opencv2/opencv.hpp>
#include <vector>
#include <string>

using namespace Pylon;
using namespace cv;

// ===== 검사 결과 구조체 =====
struct InspectionResult
{
    CString productId;      // 제품번호
    CString defectType;     // 판정결과 ("정상" / "불량" / "에러")
    CString defectDetail;   // 불량종류
    CString timestamp;      // 시간
};

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

    // ===== 카메라 =====
    CInstantCamera m_camTop;
    CInstantCamera m_camFront;
    CImageFormatConverter m_converter;
    CPylonImage m_pylonImage;
    UINT_PTR m_timerId = 0;

    // ===== 네트워크 =====
    bool m_wsaInitialized = false;

    // ===== UI 컨트롤 =====
    CListCtrl m_historyList;

    // ===== 데이터 =====
    std::vector<InspectionResult> m_history;
    int m_productCounter = 1012; // CK1012부터 시작

    // ===== 헬퍼 함수 =====
    void DrawMatToCtrl(const cv::Mat& img, CWnd* pWnd);

    // 네트워크 (응답 포함)
    bool SendImageToServer(const std::string& imgPath, std::string& response);

    // UI 업데이트
    void InitHistoryList();
    void UpdateCurrentResult(const InspectionResult& result);
    void AddToHistory(const InspectionResult& result);
    void ClearCurrentResult();

    // 히스토리 관리
    void LoadHistoryFromFile();
    void SaveHistoryToFile();

    // 유틸리티
    CString GenerateProductId();
    CString GetCurrentTimestamp();

    // JSON 파싱 (임시로 간단하게)
    bool ParseJsonResponse(const std::string& json, InspectionResult& result);
};