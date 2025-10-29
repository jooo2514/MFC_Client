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

    // ===== WSA 초기화 =====
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) == 0) {
        m_wsaInitialized = true;
        OutputDebugString(L"[INFO] WSA 초기화 완료\n");
    }

    // ===== 히스토리 리스트 초기화 =====
    InitHistoryList();

    // ===== 초기 UI 상태 =====
    ClearCurrentResult();

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
    }
    catch (const GenericException& e) {
        CString msg(e.GetDescription());
        AfxMessageBox(msg);
    }

    return TRUE;
}

// ===================== 히스토리 리스트 초기화 =====================
void CCanClientDlg::InitHistoryList()
{
    m_historyList.SubclassDlgItem(IDC_LIST_HISTORY, this);

    // ===== 컬럼 추가 =====
    m_historyList.InsertColumn(0, _T("제품번호"), LVCFMT_CENTER, 150);
    m_historyList.InsertColumn(1, _T("분석결과"), LVCFMT_CENTER, 120);
    m_historyList.InsertColumn(2, _T("불량종류"), LVCFMT_CENTER, 180);
    m_historyList.InsertColumn(3, _T("시간"), LVCFMT_CENTER, 170);

    // ===== 확장 스타일 =====
    m_historyList.SetExtendedStyle(
        LVS_EX_FULLROWSELECT |  // 전체 행 선택
        LVS_EX_GRIDLINES        // 눈금선
    );

    // ===== 파일에서 히스토리 로드 =====
    LoadHistoryFromFile();
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

// ===================== 촬영 및 전송 =====================
void CCanClientDlg::OnBnClickedBtnStart()
{
    // 타이머 일시 중지 (카메라 충돌 방지)
    if (m_timerId) {
        KillTimer(m_timerId);
    }

    GetDlgItem(IDC_BTN_START)->EnableWindow(FALSE);

    try {
        CString folder = L"C:\\CanClient\\captures";
        CreateDirectory(folder, NULL);

        if (!m_camTop.IsOpen()) m_camTop.Open();
        if (!m_camFront.IsOpen()) m_camFront.Open();

        if (!m_camTop.IsGrabbing())
            m_camTop.StartGrabbing(GrabStrategy_LatestImageOnly);
        if (!m_camFront.IsGrabbing())
            m_camFront.StartGrabbing(GrabStrategy_LatestImageOnly);

        Sleep(100); // 카메라 안정화

        CGrabResultPtr grabTop, grabFront;
        std::string topResponse, frontResponse;

        // ===== 1. TOP 이미지 캡처 및 전송 =====
        if (m_camTop.RetrieveResult(800, grabTop, TimeoutHandling_Return) &&
            grabTop->GrabSucceeded())
        {
            CImageFormatConverter converter;
            converter.OutputPixelFormat = PixelType_BGR8packed;
            CPylonImage imgTop;
            converter.Convert(imgTop, grabTop);

            Mat topMat((int)grabTop->GetHeight(), (int)grabTop->GetWidth(),
                CV_8UC3, (void*)imgTop.GetBuffer());

            std::string topPath = "C:\\CanClient\\captures\\capture_" +
                std::to_string(time(NULL)) + "_top.jpg";

            if (imwrite(topPath, topMat)) {
                OutputDebugString(L"[INFO] TOP 이미지 저장 완료\n");
                SendImageToServer(topPath, topResponse);
                OutputDebugStringA(("[TOP 응답] " + topResponse + "\n").c_str());
            }
        }

        Sleep(200); // 서버 처리 대기

        // ===== 2. FRONT 이미지 캡처 및 전송 =====
        if (m_camFront.RetrieveResult(800, grabFront, TimeoutHandling_Return) &&
            grabFront->GrabSucceeded())
        {
            CImageFormatConverter converter;
            converter.OutputPixelFormat = PixelType_BGR8packed;
            CPylonImage imgFront;
            converter.Convert(imgFront, grabFront);

            Mat frontMat((int)grabFront->GetHeight(), (int)grabFront->GetWidth(),
                CV_8UC3, (void*)imgFront.GetBuffer());

            std::string frontPath = "C:\\CanClient\\captures\\capture_" +
                std::to_string(time(NULL)) + "_front.jpg";

            if (imwrite(frontPath, frontMat)) {
                OutputDebugString(L"[INFO] FRONT 이미지 저장 완료\n");
                SendImageToServer(frontPath, frontResponse);
                OutputDebugStringA(("[FRONT 응답] " + frontResponse + "\n").c_str());

                // ===== 3. 검사 결과 처리 =====
                InspectionResult result;
                result.productId = GenerateProductId();
                result.timestamp = GetCurrentTimestamp();

                if (ParseJsonResponse(frontResponse, result)) {
                    // UI 업데이트
                    UpdateCurrentResult(result);

                    // 히스토리 추가
                    AddToHistory(result);

                    OutputDebugString(L"[SUCCESS] 검사 완료 및 결과 표시\n");
                }
                else {
                    // JSON 파싱 실패 시 간단 처리
                    result.defectType = CString(frontResponse.c_str());
                    result.defectDetail = _T("");

                    UpdateCurrentResult(result);
                    AddToHistory(result);
                }
            }
        }
    }
    catch (const GenericException& e) {
        CString msg(e.GetDescription());
        AfxMessageBox(msg);
        OutputDebugString(L"[ERROR] 카메라 에러\n");
    }

    // 타이머 재시작
    m_timerId = SetTimer(1, 33, nullptr);
    GetDlgItem(IDC_BTN_START)->EnableWindow(TRUE);
}

// ===================== TCP 전송 및 응답 수신 =====================
bool CCanClientDlg::SendImageToServer(const std::string& imgPath, std::string& response)
{
    OutputDebugString(L"[DEBUG] SendImageToServer 시작\n");

    // ===== 파일 읽기 =====
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

    // ===== 소켓 생성 =====
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        OutputDebugString(L"[ERROR] 소켓 생성 실패\n");
        return false;
    }

    // ===== 서버 연결 =====
    sockaddr_in serverAddr = {};
    serverAddr.sin_family = AF_INET;
    //serverAddr.sin_port = htons(9000);
    //inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr);

    serverAddr.sin_port = htons(9000);
    inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr);

    if (connect(sock, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        int err = WSAGetLastError();
        CString errMsg;
        errMsg.Format(L"[ERROR] 서버 연결 실패 (WSA: %d)\n", err);
        OutputDebugString(errMsg);
        closesocket(sock);
        return false;
    }
    OutputDebugString(L"[DEBUG] 서버 연결 성공\n");

    // ===== 크기 전송 (Big Endian) =====
    int fileSize = static_cast<int>(size);
    int netSize = htonl(fileSize);

    if (send(sock, (char*)&netSize, sizeof(netSize), 0) != sizeof(netSize)) {
        OutputDebugString(L"[ERROR] 길이 전송 실패\n");
        closesocket(sock);
        return false;
    }

    // ===== 데이터 전송 =====
    int totalSent = 0;
    while (totalSent < fileSize) {
        int sent = send(sock, buffer.data() + totalSent, fileSize - totalSent, 0);
        if (sent <= 0) {
            OutputDebugString(L"[ERROR] 데이터 전송 실패\n");
            closesocket(sock);
            return false;
        }
        totalSent += sent;
    }
    OutputDebugString(L"[INFO] 이미지 전송 완료\n");

    // ===== 응답 수신 =====
    char recvBuf[4096] = { 0 };
    int recvLen = recv(sock, recvBuf, sizeof(recvBuf) - 1, 0);

    if (recvLen > 0) {
        recvBuf[recvLen] = '\0';
        response = std::string(recvBuf);
        OutputDebugStringA(("[응답 수신] " + response + "\n").c_str());
    }
    else {
        OutputDebugString(L"[WARNING] 응답 없음\n");
        response = "";
    }

    closesocket(sock);
    return true;
}

// ===================== JSON 파싱 (간단 버전) =====================
bool CCanClientDlg::ParseJsonResponse(const std::string& json, InspectionResult& result)
{
    if (json.empty()) return false;

    // ===== result 필드 찾기 =====
    // 예: {"result":"정상","reason":"찌그러짐","timestamp":"2025-10-29 14:30:00"}

    std::string key = "\"result\":\"";
    size_t pos = json.find(key);
    if (pos != std::string::npos) {
        size_t start = pos + key.length();
        size_t end = json.find("\"", start);
        if (end != std::string::npos) {
            std::string resultStr = json.substr(start, end - start);
            result.defectType = CString(resultStr.c_str());
        }
    }

    // ===== reason 필드 찾기 =====
    key = "\"reason\":\"";
    pos = json.find(key);
    if (pos != std::string::npos) {
        size_t start = pos + key.length();
        size_t end = json.find("\"", start);
        if (end != std::string::npos) {
            std::string reasonStr = json.substr(start, end - start);
            result.defectDetail = CString(reasonStr.c_str());
        }
    }

    return !result.defectType.IsEmpty();
}

// ===================== UI 업데이트 =====================
void CCanClientDlg::UpdateCurrentResult(const InspectionResult& result)
{
    SetDlgItemText(IDC_STATIC_PRODUCT_ID, result.productId);
    SetDlgItemText(IDC_STATIC_DEFECT_TYPE, result.defectType);

    if (result.defectDetail.IsEmpty() || result.defectType == _T("정상")) {
        SetDlgItemText(IDC_STATIC_DEFECT_DETAIL, _T("-"));
    }
    else {
        SetDlgItemText(IDC_STATIC_DEFECT_DETAIL, result.defectDetail);
    }
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

    int idx = m_historyList.GetItemCount();

    m_historyList.InsertItem(idx, result.productId);
    m_historyList.SetItemText(idx, 1, result.defectType);
    m_historyList.SetItemText(idx, 2,
        result.defectDetail.IsEmpty() ? _T("-") : result.defectDetail);
    m_historyList.SetItemText(idx, 3, result.timestamp);

    SaveHistoryToFile();
    m_historyList.EnsureVisible(idx, FALSE);
}

// ===================== 유틸리티 함수 =====================
CString CCanClientDlg::GenerateProductId()
{
    CString productId;
    productId.Format(_T("CK%04d"), m_productCounter);
    m_productCounter++;
    return productId;
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

    CStdioFile file;
    if (!file.Open(filePath, CFile::modeCreate | CFile::modeWrite | CFile::typeText)) {
        return;
    }

    for (const auto& rec : m_history) {
        CString line;
        line.Format(_T("%s|%s|%s|%s\n"),
            rec.productId, rec.defectType,
            rec.defectDetail.IsEmpty() ? _T("-") : rec.defectDetail,
            rec.timestamp);
        file.WriteString(line);
    }
    file.Close();
}

void CCanClientDlg::LoadHistoryFromFile()
{
    CString filePath = _T("C:\\CanClient\\history.txt");
    CStdioFile file;
    if (!file.Open(filePath, CFile::modeRead | CFile::typeText)) {
        return;
    }

    CString line;
    int maxId = 1011;

    while (file.ReadString(line)) {
        int pos = 0;
        InspectionResult rec;
        rec.productId = line.Tokenize(_T("|"), pos);
        rec.defectType = line.Tokenize(_T("|"), pos);
        rec.defectDetail = line.Tokenize(_T("|"), pos);
        rec.timestamp = line.Tokenize(_T("|"), pos);

        if (rec.defectDetail == _T("-")) rec.defectDetail = _T("");

        if (!rec.productId.IsEmpty()) {
            m_history.push_back(rec);

            int idx = m_historyList.GetItemCount();
            m_historyList.InsertItem(idx, rec.productId);
            m_historyList.SetItemText(idx, 1, rec.defectType);
            m_historyList.SetItemText(idx, 2,
                rec.defectDetail.IsEmpty() ? _T("-") : rec.defectDetail);
            m_historyList.SetItemText(idx, 3, rec.timestamp);

            CString numStr = rec.productId.Mid(2);
            int num = _ttoi(numStr);
            if (num > maxId) maxId = num;
        }
    }
    m_productCounter = maxId + 1;
    file.Close();
}

// ===================== 종료 =====================
void CCanClientDlg::OnDestroy()
{
    CDialogEx::OnDestroy();

    if (m_timerId) {
        KillTimer(m_timerId);
        m_timerId = 0;
    }

    try {
        if (m_camTop.IsGrabbing()) m_camTop.StopGrabbing();
        if (m_camTop.IsOpen()) m_camTop.Close();
        if (m_camFront.IsGrabbing()) m_camFront.StopGrabbing();
        if (m_camFront.IsOpen()) m_camFront.Close();
        PylonTerminate();
    }
    catch (...) {}

    if (m_wsaInitialized) {
        WSACleanup();
        m_wsaInitialized = false;
        OutputDebugString(L"[INFO] WSA 종료\n");
    }
}