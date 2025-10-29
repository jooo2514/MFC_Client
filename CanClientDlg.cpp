#include "pch.h"
#include "framework.h"
#include "CanClient.h"
#include "CanClientDlg.h"
#include "afxdialogex.h"

#include <fstream>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#pragma warning(push)
#pragma warning(disable: 6385 6386) // 인덱스 범위 관련(예: 'this->val' 경고)
#include <nlohmann/json.hpp>
#pragma warning(pop)



// ===== JSON =====
#include <nlohmann/json.hpp>
using json = nlohmann::json;

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

using namespace cv;
using namespace Pylon;
using namespace Gdiplus;

// ===================== 메시지 맵 =====================
BEGIN_MESSAGE_MAP(CCanClientDlg, CDialogEx)
    ON_BN_CLICKED(IDC_BTN_START, &CCanClientDlg::OnBnClickedBtnStart)
    ON_WM_DESTROY()
    ON_WM_TIMER()
    ON_WM_CTLCOLOR()
    ON_NOTIFY(NM_CUSTOMDRAW, IDC_LIST_HISTORY, &CCanClientDlg::OnCustomDrawHistory)
    ON_NOTIFY(NM_DBLCLK, IDC_LIST_HISTORY, &CCanClientDlg::OnDblClkHistory)
END_MESSAGE_MAP()

// ===================== 생성자 =====================
CCanClientDlg::CCanClientDlg(CWnd* pParent)
    : CDialogEx(IDD_CANCLIENT_DIALOG, pParent)
{
    m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
}

// ===================== MFC 연결 =====================
void CCanClientDlg::DoDataExchange(CDataExchange* pDX)
{
    CDialogEx::DoDataExchange(pDX);
}

// ===================== 초기화 =====================
BOOL CCanClientDlg::OnInitDialog()
{
    CDialogEx::OnInitDialog();
    SetIcon(m_hIcon, TRUE);
    SetIcon(m_hIcon, FALSE);

    // === WSA 초기화 ===
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) == 0)
        m_wsaInitialized = true;

    try {
        PylonInitialize();
        OutputDebugString(L"[INFO] Pylon 초기화 완료\n");

        CTlFactory& factory = CTlFactory::GetInstance();
        DeviceInfoList_t devices;

        if (factory.EnumerateDevices(devices) < 2) {
            AfxMessageBox(L"Basler 카메라 2대를 모두 연결해야 합니다.");
            return TRUE;
        }

        m_camFront.Attach(factory.CreateDevice(devices[0]));  // ✅ 먼저 Front
        m_camTop.Attach(factory.CreateDevice(devices[1]));    // ✅ 그다음 Top

        m_camTop.Open();
        m_camFront.Open();

        m_camTop.StartGrabbing(GrabStrategy_LatestImageOnly);
        m_camFront.StartGrabbing(GrabStrategy_LatestImageOnly);

        // 카메라 표시용 Picture Control 스타일 보정
        GetDlgItem(IDC_CAM_TOP)->ModifyStyle(SS_BLACKFRAME, SS_WHITEFRAME);
        GetDlgItem(IDC_CAM_FRONT)->ModifyStyle(SS_BLACKFRAME, SS_WHITEFRAME);

        m_timerId = SetTimer(1, 66, nullptr); // 15fps
    }
    catch (const GenericException& e) {
        CString msg;
        msg.Format(L"[Pylon 오류] %hs\n", e.GetDescription());
        AfxMessageBox(msg);
    }

    // 검사 결과 리스트 초기화
    InitHistoryList();

    // GDI+ 초기화
    GdiplusStartupInput gdiplusStartupInput;
    GdiplusStartup(&m_gdiplusToken, &gdiplusStartupInput, nullptr);

    return TRUE;
}

// ===================== 히스토리 리스트 초기화 =====================
void CCanClientDlg::InitHistoryList()
{
    m_historyList.SubclassDlgItem(IDC_LIST_HISTORY, this);

    m_historyList.InsertColumn(0, _T("제품번호"), LVCFMT_CENTER, 150);
    m_historyList.InsertColumn(1, _T("분석결과"), LVCFMT_CENTER, 120);
    m_historyList.InsertColumn(2, _T("불량종류"), LVCFMT_CENTER, 180);
    m_historyList.InsertColumn(3, _T("시간"), LVCFMT_CENTER, 170);

    m_historyList.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    LoadHistoryFromFile();
}

// ===================== 타이머 (프레임 루프) =====================
void CCanClientDlg::OnTimer(UINT_PTR nIDEvent)
{
    if (nIDEvent == 1)
    {
        try {
            Pylon::CGrabResultPtr grabTop, grabFront;

            // ===== TOP 카메라 =====
            if (m_camTop.IsGrabbing() &&
                m_camTop.RetrieveResult(150, grabTop, Pylon::TimeoutHandling_Return) &&
                grabTop->GrabSucceeded())
            {
                Pylon::CImageFormatConverter converter;
                converter.OutputPixelFormat = Pylon::PixelType_BGR8packed;
                converter.OutputBitAlignment = Pylon::OutputBitAlignment_MsbAligned;

                Pylon::CPylonImage imgTop;
                converter.Convert(imgTop, grabTop);

                cv::Mat frame((int)grabTop->GetHeight(), (int)grabTop->GetWidth(),
                    CV_8UC3, (void*)imgTop.GetBuffer());

                OutputDebugString(L"[TOP] frame OK\n");
                DrawMatToCtrl(frame.clone(), GetDlgItem(IDC_CAM_TOP));
            }
            else {
                OutputDebugString(L"[TOP] RetrieveResult 미응답 (Frame skip)\n");
            }

            // ===== FRONT 카메라 =====
            if (m_camFront.IsGrabbing() &&
                m_camFront.RetrieveResult(150, grabFront, Pylon::TimeoutHandling_Return) &&
                grabFront->GrabSucceeded())
            {
                Pylon::CImageFormatConverter converter;
                converter.OutputPixelFormat = Pylon::PixelType_BGR8packed; // ✅ 컬러 유지
                converter.OutputBitAlignment = Pylon::OutputBitAlignment_MsbAligned;

                Pylon::CPylonImage imgFront;
                converter.Convert(imgFront, grabFront);

                cv::Mat frame((int)grabFront->GetHeight(), (int)grabFront->GetWidth(),
                    CV_8UC3, (void*)imgFront.GetBuffer());

                OutputDebugString(L"[FRONT] frame OK\n");

                // 혹시라도 1채널로 들어올 경우 대비
                if (frame.channels() == 1)
                    cv::cvtColor(frame, frame, cv::COLOR_GRAY2BGR);

                DrawMatToCtrl(frame.clone(), GetDlgItem(IDC_CAM_FRONT));
            }
            else {
                OutputDebugString(L"[FRONT] RetrieveResult 미응답 (Frame skip)\n");
            }
        }
        catch (const Pylon::GenericException& e) {
            CString msg;
            msg.Format(L"[Basler 예외] %hs\n", e.GetDescription());
            OutputDebugString(msg);
        }
        catch (const cv::Exception& e) {
            OutputDebugStringA(("[OpenCV 예외] " + std::string(e.what()) + "\n").c_str());
        }
        catch (...) {
            OutputDebugString(L"[OnTimer] 알 수 없는 예외 발생\n");
        }
    }

    CDialogEx::OnTimer(nIDEvent);
}


// ===================== 이미지 표시 =====================
void CCanClientDlg::DrawMatToCtrl(const cv::Mat& img, CWnd* pWnd)
{
    if (!pWnd || !::IsWindow(pWnd->GetSafeHwnd())) return;

    CClientDC dc(pWnd);
    CRect rc;
    pWnd->GetClientRect(&rc);

    cv::Mat displayImg;
    if (img.empty())
    {
        displayImg = cv::Mat(rc.Height(), rc.Width(), CV_8UC3, cv::Scalar(0, 0, 0));
    }
    else
    {
        img.copyTo(displayImg);
        if (displayImg.channels() == 1)
            cv::cvtColor(displayImg, displayImg, cv::COLOR_GRAY2BGR);
        cv::resize(displayImg, displayImg,
            cv::Size(rc.Width(), rc.Height()),
            0, 0, cv::INTER_AREA);
    }

    try {
        Gdiplus::Graphics g(dc.GetSafeHdc());
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        g.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);

        // 안전한 비트맵 변환 및 출력
        Gdiplus::Bitmap bmp(displayImg.cols, displayImg.rows,
            (INT)displayImg.step, PixelFormat24bppRGB, displayImg.data);
        g.DrawImage(&bmp, 0, 0, rc.Width(), rc.Height());
    }
    catch (...) {
        OutputDebugString(L"[DrawMatToCtrl] GDI+ DrawImage 실패\n");
        dc.FillSolidRect(&rc, RGB(0, 0, 0));
    }
}


// ===================== 촬영 및 전송 =====================
void CCanClientDlg::OnBnClickedBtnStart()
{
    if (m_timerId) KillTimer(m_timerId);
    GetDlgItem(IDC_BTN_START)->EnableWindow(FALSE);

    try {
        CString folder = L"C:\\CanClient\\captures";
        CreateDirectory(folder, NULL);

        if (!m_camTop.IsOpen())   m_camTop.Open();
        if (!m_camFront.IsOpen()) m_camFront.Open();
        if (!m_camTop.IsGrabbing())   m_camTop.StartGrabbing(GrabStrategy_LatestImageOnly);
        if (!m_camFront.IsGrabbing()) m_camFront.StartGrabbing(GrabStrategy_LatestImageOnly);

        Sleep(100);

        CGrabResultPtr grabTop, grabFront;
        std::string topPath, frontPath;
        std::string topResponse, frontResponse;

        // TOP
        if (m_camTop.RetrieveResult(800, grabTop, TimeoutHandling_Return) &&
            grabTop->GrabSucceeded())
        {
            CImageFormatConverter converter; converter.OutputPixelFormat = PixelType_BGR8packed;
            CPylonImage imgTop; converter.Convert(imgTop, grabTop);
            Mat topMat((int)grabTop->GetHeight(), (int)grabTop->GetWidth(), CV_8UC3, (void*)imgTop.GetBuffer());

            topPath = "C:\\CanClient\\captures\\capture_" + std::to_string(time(NULL)) + "_top.jpg";
            if (imwrite(topPath, topMat)) {
                OutputDebugString(L"[INFO] TOP 이미지 저장 완료\n");
                SendImageToServer(topPath, topResponse);
                OutputDebugStringA(("[TOP 응답] " + topResponse + "\n").c_str());
            }
        }

        Sleep(200);

        // FRONT
        if (m_camFront.RetrieveResult(800, grabFront, TimeoutHandling_Return) &&
            grabFront->GrabSucceeded())
        {
            CImageFormatConverter converter; converter.OutputPixelFormat = PixelType_BGR8packed;
            CPylonImage imgFront; converter.Convert(imgFront, grabFront);
            Mat frontMat((int)grabFront->GetHeight(), (int)grabFront->GetWidth(), CV_8UC1, (void*)imgFront.GetBuffer());
            cv::cvtColor(frontMat, frontMat, cv::COLOR_GRAY2BGR);

            frontPath = "C:\\CanClient\\captures\\capture_" + std::to_string(time(NULL)) + "_front.jpg";
            if (imwrite(frontPath, frontMat)) {
                OutputDebugString(L"[INFO] FRONT 이미지 저장 완료\n");
                SendImageToServer(frontPath, frontResponse);
                OutputDebugStringA(("[FRONT 응답] " + frontResponse + "\n").c_str());
            }
        }

        // 결과
        InspectionResult result;
        result.productId = GenerateProductId();
        result.timestamp = GetCurrentTimestamp();
        result.imgTopPath = CString(topPath.c_str());
        result.imgFrontPath = CString(frontPath.c_str());

        if (ParseJsonResponse(frontResponse, result)) {
            UpdateCurrentResult(result);
            AddToHistory(result);
        }
        else {
            result.defectType = _T("에러");
            result.defectDetail = CString(frontResponse.c_str());
            UpdateCurrentResult(result);
            AddToHistory(result);
        }
    }
    catch (const GenericException& e) {
        AfxMessageBox(CString(e.GetDescription()));
    }

    m_timerId = SetTimer(1, 33, nullptr);
    GetDlgItem(IDC_BTN_START)->EnableWindow(TRUE);
}

// ===================== TCP 전송 및 응답 수신 =====================
bool CCanClientDlg::SendImageToServer(const std::string& imgPath, std::string& response)
{
    OutputDebugString(L"[DEBUG] SendImageToServer 시작\n");

    std::ifstream file(imgPath, std::ios::binary | std::ios::ate);
    if (!file) { OutputDebugString(L"[ERROR] 이미지 파일 열기 실패\n"); return false; }
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<char> buffer(size);
    if (!file.read(buffer.data(), size)) { OutputDebugString(L"[ERROR] 파일 읽기 실패\n"); return false; }

    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) { OutputDebugString(L"[ERROR] 소켓 생성 실패\n"); return false; }

    sockaddr_in serverAddr = {};
    serverAddr.sin_family = AF_INET;
    //serverAddr.sin_port = htons(9000);
    //inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr); // 테스트

    serverAddr.sin_port = htons(9000);
    inet_pton(AF_INET, "10.10.21.121", &serverAddr.sin_addr); // 테스트

    if (connect(sock, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        CString msg; msg.Format(L"[ERROR] 서버 연결 실패 (WSA: %d)\n", WSAGetLastError());
        OutputDebugString(msg); closesocket(sock); return false;
    }
    OutputDebugString(L"[DEBUG] 서버 연결 성공\n");

    int fileSize = static_cast<int>(size);
    int netSize = htonl(fileSize);
    if (send(sock, (char*)&netSize, sizeof(netSize), 0) != sizeof(netSize)) {
        OutputDebugString(L"[ERROR] 길이 전송 실패\n"); closesocket(sock); return false;
    }

    int totalSent = 0;
    while (totalSent < fileSize) {
        int sent = send(sock, buffer.data() + totalSent, fileSize - totalSent, 0);
        if (sent <= 0) { OutputDebugString(L"[ERROR] 데이터 전송 실패\n"); closesocket(sock); return false; }
        totalSent += sent;
    }
    OutputDebugString(L"[INFO] 이미지 전송 완료\n");

    char recvBuf[4096] = { 0 };
    int recvLen = recv(sock, recvBuf, sizeof(recvBuf) - 1, 0);
    if (recvLen > 0) { recvBuf[recvLen] = '\0'; response.assign(recvBuf); }
    else { OutputDebugString(L"[WARNING] 응답 없음\n"); response.clear(); }

    closesocket(sock);
    return true;
}

// ===================== JSON 파싱 =====================
bool CCanClientDlg::ParseJsonResponse(const std::string& jsonStr, InspectionResult& result)
{
    if (jsonStr.empty()) return false;

    try {
        auto j = json::parse(jsonStr);

        // ===================== result =====================
        if (j.contains("result")) {
            std::string s = j["result"].get<std::string>();

            // 문자열을 소문자로 변환해서 단일 비교
            std::string lower;
            lower.resize(s.size());
            std::transform(s.begin(), s.end(), lower.begin(), ::tolower);

            if (lower == "ok" || lower == "pass" || lower == "정상" || lower == "1")
                result.defectType = _T("정상");
            else if (lower == "ng" || lower == "fail" || lower == "불량" || lower == "2")
                result.defectType = _T("불량");
            else
                result.defectType = _T("에러"); // 기타 기호나 ? 등
        }
        else {
            result.defectType = _T("에러");
        }

        // ===================== reason =====================
        if (j.contains("reason")) {
            std::string s = j["reason"].get<std::string>();
            result.defectDetail = CString(s.c_str());
        }
        else {
            result.defectDetail.Empty();
        }

        return true;
    }
    catch (...) {
        OutputDebugString(L"[ERROR] JSON 파싱 실패\n");
        result.defectType = _T("에러");
        result.defectDetail.Empty();
        return false;
    }
}


// ===================== UI 업데이트 =====================
void CCanClientDlg::UpdateCurrentResult(const InspectionResult& result)
{
    SetDlgItemText(IDC_STATIC_PRODUCT_ID, result.productId);
    SetDlgItemText(IDC_STATIC_DEFECT_TYPE, result.defectType);

    if (result.defectDetail.IsEmpty() || result.defectType == _T("정상"))
        SetDlgItemText(IDC_STATIC_DEFECT_DETAIL, _T("-"));
    else
        SetDlgItemText(IDC_STATIC_DEFECT_DETAIL, result.defectDetail);

    if (result.defectType == _T("정상"))      m_currResultColor = RGB(34, 177, 76);
    else if (result.defectType == _T("불량"))  m_currResultColor = RGB(237, 28, 36);
    else                                       m_currResultColor = RGB(128, 128, 128);

    GetDlgItem(IDC_STATIC_DEFECT_TYPE)->Invalidate();
}

void CCanClientDlg::ClearCurrentResult()
{
    SetDlgItemText(IDC_STATIC_PRODUCT_ID, _T("-"));
    SetDlgItemText(IDC_STATIC_DEFECT_TYPE, _T("-"));
    SetDlgItemText(IDC_STATIC_DEFECT_DETAIL, _T("-"));
}

void CCanClientDlg::AddToHistory(const InspectionResult& result)
{
    m_history.push_back(result);
    const int idx = m_historyList.GetItemCount();

    m_historyList.InsertItem(idx, result.productId);
    m_historyList.SetItemText(idx, 1, result.defectType);
    m_historyList.SetItemText(idx, 2, result.defectDetail.IsEmpty() ? _T("-") : result.defectDetail);
    m_historyList.SetItemText(idx, 3, result.timestamp);

    SaveHistoryToFile();
    m_historyList.EnsureVisible(idx, FALSE);
    UpdateStats();
}

// ===================== 유틸리티 =====================
CString CCanClientDlg::GenerateProductId()
{
    CString id; id.Format(_T("%d"), m_productCounter++);
    return id;
}

CString CCanClientDlg::GetCurrentTimestamp()
{
    CTime now = CTime::GetCurrentTime();
    return now.Format(_T("%Y-%m-%d %H:%M:%S"));
}

// ===================== 히스토리 저장/로드 =====================
void CCanClientDlg::SaveHistoryToFile()
{
    CString folder = _T("C:\\CanClient");
    CreateDirectory(folder, NULL);
    CString filePath = folder + _T("\\history.txt");

    // 최대 100건 유지
    const size_t MAX_HISTORY = 100;
    if (m_history.size() > MAX_HISTORY)
        m_history.erase(m_history.begin(), m_history.end() - MAX_HISTORY);

    CStdioFile file;
    if (!file.Open(filePath, CFile::modeCreate | CFile::modeWrite | CFile::typeText))
        return;

    for (const auto& rec : m_history)
    {
        if (rec.productId.IsEmpty()) continue; // 숫자 문자열이면 OK

        CString line;
        line.Format(_T("%s|%s|%s|%s\n"),
            rec.productId,
            rec.defectType.IsEmpty() ? _T("-") : rec.defectType,
            rec.defectDetail.IsEmpty() ? _T("-") : rec.defectDetail,
            rec.timestamp.IsEmpty() ? _T("-") : rec.timestamp);
        file.WriteString(line);
    }
    file.Close();
}

void CCanClientDlg::LoadHistoryFromFile()
{
    CString filePath = _T("C:\\CanClient\\history.txt");
    CStdioFile file;

    m_history.clear();
    if (m_historyList.GetSafeHwnd()) m_historyList.DeleteAllItems();

    if (!file.Open(filePath, CFile::modeRead | CFile::typeText)) {
        m_productCounter = 1;
        UpdateStats();
        return;
    }

    CString line;
    int maxId = 0;

    while (file.ReadString(line))
    {
        line.Trim();
        if (line.IsEmpty()) continue;

        int pos = 0;
        InspectionResult rec;
        rec.productId = line.Tokenize(_T("|"), pos);
        rec.defectType = line.Tokenize(_T("|"), pos);
        rec.defectDetail = line.Tokenize(_T("|"), pos);
        rec.timestamp = line.Tokenize(_T("|"), pos);

        if (rec.productId.IsEmpty()) continue;
        if (rec.defectDetail == _T("-")) rec.defectDetail.Empty();

        m_history.push_back(rec);

        if (m_historyList.GetSafeHwnd()) {
            const int idx = m_historyList.GetItemCount();
            m_historyList.InsertItem(idx, rec.productId);
            m_historyList.SetItemText(idx, 1, rec.defectType.IsEmpty() ? _T("-") : rec.defectType);
            m_historyList.SetItemText(idx, 2, rec.defectDetail.IsEmpty() ? _T("-") : rec.defectDetail);
            m_historyList.SetItemText(idx, 3, rec.timestamp.IsEmpty() ? _T("-") : rec.timestamp);
        }

        int num = _ttoi(rec.productId); // 숫자 변환
        if (num > maxId) maxId = num;
    }
    file.Close();

    // 파일이 비어있으면 1부터, 아니면 다음 번호
    m_productCounter = (maxId <= 0) ? 1 : (maxId + 1);
    UpdateStats();
}

// ===================== 종료 =====================
void CCanClientDlg::OnDestroy()
{
    CDialogEx::OnDestroy();

    if (m_timerId) { KillTimer(m_timerId); m_timerId = 0; }

    try {
        if (m_camTop.IsGrabbing())   m_camTop.StopGrabbing();
        if (m_camTop.IsOpen())       m_camTop.Close();
        if (m_camFront.IsGrabbing()) m_camFront.StopGrabbing();
        if (m_camFront.IsOpen())     m_camFront.Close();
        PylonTerminate();
    }
    catch (...) {}

    if (m_wsaInitialized) { WSACleanup(); m_wsaInitialized = false; }
    if (m_gdiplusToken) {
        Gdiplus::GdiplusShutdown(m_gdiplusToken);
        m_gdiplusToken = 0;
    }

}

// ===================== 색상/리스트/더블클릭 =====================
HBRUSH CCanClientDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
    HBRUSH hbr = CDialogEx::OnCtlColor(pDC, pWnd, nCtlColor);
    if (pWnd->GetDlgCtrlID() == IDC_STATIC_DEFECT_TYPE) {
        pDC->SetTextColor(m_currResultColor);
        pDC->SetBkMode(TRANSPARENT);
        return (HBRUSH)GetStockObject(HOLLOW_BRUSH);
    }
    return hbr;
}

void CCanClientDlg::OnCustomDrawHistory(NMHDR* pNMHDR, LRESULT* pResult)
{
    LPNMLVCUSTOMDRAW pCD = reinterpret_cast<LPNMLVCUSTOMDRAW>(pNMHDR);

    switch (pCD->nmcd.dwDrawStage)
    {
    case CDDS_PREPAINT:
        *pResult = CDRF_NOTIFYITEMDRAW;
        return;

    case CDDS_ITEMPREPAINT:
    {
        int idx = static_cast<int>(pCD->nmcd.dwItemSpec);
        if (idx >= 0 && idx < (int)m_history.size())
            if (m_history[idx].defectType == _T("불량"))
                pCD->clrText = RGB(237, 28, 36);

        *pResult = CDRF_DODEFAULT;
        return;
    }

    default:
        break; // 명시적 종료로 fallthrough 경고 제거
    }

    *pResult = 0;
}


void CCanClientDlg::UpdateStats()
{
    // 오늘 날짜 yyyy-mm-dd만 비교 (길이 체크 방어)
    CTime now = CTime::GetCurrentTime();
    CString today = now.Format(_T("%Y-%m-%d"));

    int totalToday = 0, ok = 0, ng = 0;
    for (const auto& r : m_history) {
        CString head = (r.timestamp.GetLength() >= 10) ? r.timestamp.Left(10) : _T("");
        if (head == today) {
            totalToday++;
            if (r.defectType == _T("정상")) ok++;
            else if (r.defectType == _T("불량")) ng++;
        }
    }

    CString s1; s1.Format(_T("오늘 검사: %d건"), totalToday);
    CString s2; s2.Format(_T("정상 %d / 불량 %d"), ok, ng);
    double rate = (totalToday > 0) ? (100.0 * ok / totalToday) : 0.0;
    CString s3; s3.Format(_T("정상비율: %.1f%%"), rate);

    SetDlgItemText(IDC_STATIC_TODAY_CNT, s1);
    SetDlgItemText(IDC_STATIC_OK_NG, s2);
    SetDlgItemText(IDC_STATIC_RATE, s3);
}

// ===================== 미리보기 다이얼로그 =====================
BOOL CPreviewDlg::OnInitDialog()
{
    CDialogEx::OnInitDialog();
    LoadToCtrl(IDC_IMG_LEFT, m_left);
    LoadToCtrl(IDC_IMG_RIGHT, m_right);
    return TRUE;
}


void CPreviewDlg::LoadToCtrl(int id, const CString& path)
{
    if (path.IsEmpty()) return;
    CImage img;
    if (SUCCEEDED(img.Load(path))) {
        CStatic* st = (CStatic*)GetDlgItem(id);
        if (!st) return;
        CDC* pDC = st->GetDC();
        CRect rc; st->GetClientRect(&rc);
        img.Draw(*pDC, 0, 0, rc.Width(), rc.Height(), 0, 0, img.GetWidth(), img.GetHeight());
        st->ReleaseDC(pDC);
    }
}

void CCanClientDlg::OnDblClkHistory(NMHDR* pNMHDR, LRESULT* pResult)
{
    POSITION pos = m_historyList.GetFirstSelectedItemPosition();
    if (!pos) return;
    int idx = m_historyList.GetNextSelectedItem(pos);
    if (idx < 0 || idx >= (int)m_history.size()) return;

    const auto& rec = m_history[idx];
    CPreviewDlg dlg(rec.imgTopPath, rec.imgFrontPath);
    dlg.DoModal();

    *pResult = 0;
}
