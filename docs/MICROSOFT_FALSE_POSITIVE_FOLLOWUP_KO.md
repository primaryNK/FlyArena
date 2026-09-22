# Microsoft 탐지 심사 결과 후속 조치

결과가 나오면 아래 항목을 그대로 복사해 Codex에게 전달하세요.

```text
Microsoft submission ID:
Final determination:
Detection name:
Microsoft가 제공한 설명:
현재 Defender 보안 인텔리전스 버전:
정확한 SHA-256 AE83D55159A9E22682B3028FBCE67C7470F9805EE056D52D1250C7AC727B4190의 재검사 결과:
```

`No malware` 또는 `False positive` 판정이면 Defender 정의를 갱신한 뒤 정확한
파일을 다시 검사합니다. 그 다음 동일 소스에서 새로 빌드하고, GitHub에서
내려받은 파일까지 재검사해서 둘 다 통과할 때만 Windows Release를 복구합니다.

`Malware` 또는 `Suspicious` 판정이면 실행 파일을 다시 공개하지 않습니다.
Microsoft의 상세 설명을 보존하고 소스, GitHub Actions 빌드 로그, 의존 도구,
저장소 접근 권한을 별도로 감사합니다.

`Insufficient information`, `Pending` 또는 판정이 모호하면 submission ID와
전체 답변을 전달하세요. 같은 파일을 해시만 바꿔 다시 배포하지 않습니다.

SmartScreen의 `unrecognized app` 경고와 Defender Antivirus의
`Trojan:Win32/Wacatac.B!ml` 탐지는 서로 다릅니다. 결과 화면에서 어떤 제품의
판정인지도 함께 알려 주세요.
