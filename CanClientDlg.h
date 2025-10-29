#pragma once
#include <pylon/PylonIncludes.h>
#include <opencv2/opencv.hpp>
#include <vector>
#include <string>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

using namespace Pylon;
using namespace cv;

// ===== 검사 결과 구조체 =====
struct InspectionResult
{
    CString productId;      // 제품번호 (단순 1,2,3,...)
    CString defectType;     // 판정결과 ("정상" / "불량" / "에러")
    CString defectDetail;   // 불량종류
    CString timestamp;      // 시간
    CString imgTopPath;     // 상단 이미지 경로
    CString imgFrontPath;   // 정면 이미지 경로
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
    ULONG_PTR m_gdiplusToken = 0; // GDI+ Startup/Shutdown용
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
    int m_productCounter = 1; // ← 단순 1부터 시작

    // ===== 헬퍼 함수 =====
    void DrawMatToCtrl(const cv::Mat& img, CWnd* pWnd);

    // 네트워크 (응답 포함)
    bool SendImageToServer(const std::string& imgPath, std::string& response);

    // UI 업데이트
    void InitHistoryList();
    void UpdateCurrentResult(const InspectionResult& result);
    void AddToHistory(const InspectionResult& result);
    void ClearCurrentResult();

    // 색상 표시/리스트 커스텀 드로우/더블클릭
    COLORREF m_currResultColor = RGB(0, 0, 0);
    CString  m_currResultText;
    afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
    afx_msg void OnCustomDrawHistory(NMHDR* pNMHDR, LRESULT* pResult);
    afx_msg void OnDblClkHistory(NMHDR* pNMHDR, LRESULT* pResult);

    // 히스토리 관리
    void LoadHistoryFromFile();
    void SaveHistoryToFile();

    // 유틸리티
    CString GenerateProductId();
    CString GetCurrentTimestamp();

    // JSON 파싱
    bool ParseJsonResponse(const std::string& json, InspectionResult& result);

    // 통계
    void UpdateStats();
};

// ===================== 이미지 미리보기 다이얼로그 =====================
class CPreviewDlg : public CDialogEx
{
public:
    CPreviewDlg(const CString& left, const CString& right)
        : CDialogEx(IDD_PREVIEW_DLG), m_left(left), m_right(right) {
    }

protected:
    virtual BOOL OnInitDialog();

private:
    CString m_left, m_right;
    void LoadToCtrl(int id, const CString& path);
};
