# FlyArena

FlyArena는 BANC v888/v3 초파리 연결망으로 두 개의 독립적인 신경 상태를
계산하고, 그 저수준 출력으로 물리 전투장을 움직이는 Windows 실험형
프로젝트입니다. 쉽게 말해, 미리 정해진 `공격()` 명령을 실행하는 게임이
아니라 신경 출력이 검·방패·이동 장치를 움직이고 충돌과 타이밍이 결과를
정합니다.

이 프로젝트는 개발 과정에서 **GPT와 Codex를 적극적으로 활용한 바이브코딩
(Vibe Coding) 방식**으로 제작되었습니다. 아이디어 설계, 구조 논의, 디버깅,
테스트 설계, 문서화와 반복적인 코드 개선 과정에서 AI와 협업했으며, 최종적인
방향 결정과 실행·검증은 실제 프로젝트 환경에서 진행했습니다.

The last user-validated stable combat baseline is **v0.6.4d**. The current
experimental build is **v0.6.8**: source-build setup, full-loop speed control,
compute-limited MAX learning, canonical sword reach, body/wing hitboxes,
opposing spawns, and Flypack v4 customization.

## Current architecture

* BANC v888/v3: 188,508 neurons and 13,620,865 directed pairs
* two independent D3D12 neural-state sets at a 1 ms internal timestep
* sensory/world control at 50 ms
* physical combat substeps no larger than 5 ms
* Win32 + Direct2D/DirectWrite renderer, decoupled from simulation
* low-level forward, turn, sword-drive, and shield-drive actuators only
* HIT/BLOCK/PARRY/DODGE classified from physical geometry and timing

The learner changes a small motor readout layer. It does not modify the BANC
connectivity pairs and does not add scripted actions such as `attack()` or
`parry()`.

## 현재 배포 상태

> **Windows 실행 파일 배포 일시 중단:** 현재 서명되지 않은 v0.6.8 실행
> 파일은 로컬 및 GitHub Actions의 Microsoft Defender 검사를 통과하지만,
> 인터넷에서 내려받을 때 Defender 클라우드 검사에서
> `Trojan:Win32/Wacatac.B!ml`로 탐지됩니다. 현재 조사 결과로는 오탐이
> 의심되지만 Microsoft 분석 전에는 확정할 수 없습니다. 경고를 무시하거나
> Defender를 끄지 마세요. Microsoft가 탐지를 해제하거나, 신뢰할 수 있는
> Authenticode 서명을 적용한 대체 빌드가 인터넷 다운로드 검사를 통과할
> 때까지 Release에는 검증 가능한 소스 ZIP만 제공합니다.

현재 GitHub Release에는 다음 두 파일만 있습니다.

- `FlyArena-v0.6.8-Source.zip` — 직접 빌드할 소스 코드
- `SHA256SUMS.txt` — Source ZIP 무결성 확인용 체크섬

`FlyArena-v0.6.8-Windows-x64.zip`은 현재 제공하지 않습니다. 다른 사람이
재업로드한 EXE를 받거나 Defender 경고를 무시하지 마세요.

## 처음 다운로드부터 실행까지

### 1. 준비물

- Windows 10/11 x64
- Direct3D 12를 지원하는 GPU와 최신 그래픽 드라이버
- [Visual Studio Community](https://visualstudio.microsoft.com/vs/community/)
  또는 Build Tools. 설치 항목에서 **Desktop development with C++**, MSVC x64,
  Windows 10/11 SDK를 선택합니다.
- [Python 3](https://www.python.org/downloads/windows/). 설치할 때
  **Add Python to PATH**를 선택합니다.
- 인터넷 연결과 BANC 원자료·변환 파일을 저장할 여유 공간

Git은 필요하지 않습니다. Source ZIP만으로 빌드하고 실행할 수 있습니다.

### 2. Source ZIP 다운로드 및 확인

1. [FlyArena v0.6.8 Release](https://github.com/primaryNK/FlyArena/releases/tag/v0.6.8)의
   **Assets**에서 `FlyArena-v0.6.8-Source.zip`과 `SHA256SUMS.txt`를 받습니다.
2. 두 파일이 있는 폴더에서 PowerShell을 열고 다음 명령을 실행합니다.

```powershell
Get-FileHash .\FlyArena-v0.6.8-Source.zip -Algorithm SHA256
Get-Content .\SHA256SUMS.txt
```

두 SHA-256 값이 같아야 합니다. 다르면 압축을 풀거나 실행하지 말고 다시
다운로드하세요.

### 3. 압축 해제

ZIP을 원하는 폴더에 완전히 풉니다. 이후 명령과 배치 파일은
`README.md`, `build_v068.bat`, `SETUP_DATA_AND_RUN.bat`이 보이는 프로젝트
최상위 폴더에서 실행합니다. ZIP 내부에서 직접 실행하지 마세요.

### 4. 가장 쉬운 첫 실행

`SETUP_DATA_AND_RUN.bat`을 실행합니다. Source ZIP에서는 이 배치가 다음 작업을
순서대로 수행합니다.

1. `build_v068.bat`으로 `bin\FlyArena-v0.6.8.exe` 빌드
2. 공식 BANC v888 원자료 다운로드
3. 프로젝트 전용 Python 환경과 필요한 패키지 준비
4. topology cache와 IO map 생성
5. FlyArena 실행

첫 데이터 준비는 다운로드와 변환 때문에 오래 걸릴 수 있습니다. 성공한
파일은 다음 실행에서 재사용되며 매 라운드 다시 다운로드하지 않습니다.

### 5. 단계별로 직접 실행하는 방법

한 번에 실행하지 않으려면 Developer PowerShell 또는 일반 명령 프롬프트에서
다음 순서로 실행합니다.

```bat
build_v068.bat
test_v068_profile_learning.bat
prepare_banc_latest.bat
prepare_v061_io_map.bat
START_FLYARENA.bat
```

빌드 결과는 `bin\FlyArena-v0.6.8.exe`입니다. 테스트는 `.flypack` 왕복,
학습 저장·고정, 장비 파생값, 검 충돌, BLOCK/PARRY, 날개 hitbox, 스태미나와
경기 종료 규칙을 확인합니다.

### 6. 다음 실행

- `START_FLYARENA.bat` — 기존 빌드와 준비된 데이터를 바로 실행
- `run_v068_learning_arena.bat` — 다시 빌드하고 Training으로 실행
- `run_v068_battle_arena.bat` — 다시 빌드하고 학습이 고정된 Battle로 실행

소스를 수정했다면 `START_FLYARENA.bat` 전에 `build_v068.bat`을 다시
실행하세요.

### 7. GPU 사용률과 배속

- 1x~16x와 Battle은 기본 `--gpu-budget 40` 제한을 사용합니다.
- 더 낮추려면 `run_v068_learning_arena.bat` 또는
  `run_v068_battle_arena.bat`의 `--gpu-budget 40`을 `20`처럼 낮춥니다.
- `MAX`는 벽시계 대기와 GPU 제한을 제거하고 가능한 최고 속도로 학습하므로
  GPU 사용률이 매우 높은 것이 정상입니다. 낮은 사용률이 필요하면 16x 이하를
  사용하세요.
- v0.6.8의 제한 계산은 신경 커널뿐 아니라 감각 업로드와 뉴런 결과 readback
  시간도 포함합니다.

### 8. 자주 생기는 문제

- `vswhere.exe not found` 또는 C++ toolchain 오류: Visual Studio Installer에서
  **Desktop development with C++**와 Windows SDK를 설치합니다.
- `Python 3 not found`: Python을 PATH에 추가한 뒤 새 터미널에서 다시 실행합니다.
- `topology load failed`: `prepare_banc_latest.bat`과
  `prepare_v061_io_map.bat`을 순서대로 다시 실행합니다.
- MAX 화면이 끊겨 보임: 계산에 자원을 집중하기 위해 화면은 낮은 빈도로만
  갱신됩니다. 내부 시뮬레이션과 학습은 계속 진행됩니다.
- 직접 빌드한 EXE도 Defender가 탐지: 경고를 우회하지 말고 EXE의 SHA-256,
  탐지명, Defender 보안 인텔리전스 버전을 이슈에 적어 주세요.

더 자세한 설명은 [한국어 시작 안내](docs/GETTING_STARTED_KO.md)를 참고하세요.

## 화면과 실행 모드

The renderer provides:

* `CREATE FLY` — save a validated, data-only `.flypack`
* Create Fly has three linked gameplay controls: sword length, wing
  speed/endurance trade-off, and shield weight
* six physics-invariant silhouettes per body/equipment category and separate
  body/wing/sword/shield two-color gradients shared by preview and game
* `TRAINING` / `BATTLE` — cancel the current match and start the selected mode
* `PLAY` / `PAUSE`
* `REPEAT` — automatically start the next episode
* `RESET` — cancel and restart the current mode
* dotted HUD border — drop one `.flytrain`; the displayed `Save:` path becomes
  the active checkpoint for that slot
* `RESET THIS FLY LEARNING` — Training-only, confirmed reset of the displayed
  checkpoint
* each HUD shows `TRAINED N`, the completed Training-match count stored in the
  active `.flytrain`; cancelled matches and frozen Battle do not increment it
* `SOUND`, `VOL -`, and `VOL +` — in-game mute and volume control in 10% steps

The first run loads and calibrates the CNS once. Repeated rounds reuse the same
resident topology, GPU resources, envelope, and evolving neural state. 1x–16x
pace the complete simulation tick. `MAX` removes wall-clock pacing, suppresses
per-tick visualization/CSV work, freezes the displayed flies at opposite spawn
points, and advances the hidden fight at the hardware limit. Simulation
timesteps do not change.

## Persistent data

* identity/equipment/cosmetics: `data\flies\*.flypack`
* learned plastic readout: `data\training\*.flytrain`
* telemetry and latest summaries: `results\`

Fly packages are data-only. Imported packages cannot load DLLs, scripts,
shaders, or arbitrary filesystem paths.

### 초파리 공유

`data\flies` 폴더의 `.flypack` 파일은 각 초파리의 이름, 외형, 장비와 같은
프로필 데이터를 담고 있습니다.

자신이 만든 초파리를 다른 사람과 공유하고 싶다면 해당 `.flypack` 파일을
전달하면 됩니다. 다른 사용자는 받은 `.flypack`을 자신의 `data\flies`
폴더에 넣어 FlyArena에서 불러올 수 있습니다.

예를 들어:

```text
data/
└─ flies/
   ├─ Ruby.flypack
   ├─ Azure.flypack
   └─ MyFly.flypack
```

처럼 여러 초파리를 보관할 수 있습니다.

GitHub의 `data/flies` 폴더에는 예제 또는 공유용 초파리 데이터를 둘 수 있으며,
사용자들이 만든 초파리를 서로 교환해 같은 장비·외형 설정을 불러오는 것도
가능합니다.

학습 상태는 `.flypack`과 별도로 `.flytrain`에 저장됩니다. 따라서 외형과 장비만
공유할 수도 있고, 별도의 `.flytrain` 파일을 함께 전달해 학습된 초파리의
상태를 이어서 사용하거나 대전시킬 수도 있습니다.

개인적으로 장기간 학습한 `.flytrain` 파일은 기본 Git 저장소에는 포함하지 않는
것을 권장합니다.

관심 있는 개발자와 연구자는 [문서 안내](docs/README.md)에서 현재 구조와
가정을 확인할 수 있습니다. 공개 Git과 소스 ZIP에는 현재 v0.6.8에 필요한
최종 파일만 들어가며, 개발 과정의 옛 빌드·실험 파일은 포함하지 않습니다.

## Development approach

FlyArena는 전통적인 수동 코딩만으로 제작된 프로젝트가 아니라, GPT 계열
모델 및 Codex와 대화하면서 아이디어를 빠르게 구현하고 실제 실행 결과를
반복적으로 검증하는 **AI-assisted / Vibe Coding** 방식으로 개발되었습니다.

AI가 생성하거나 제안한 코드도 그대로 신뢰하지 않고 실제 Windows 환경에서
빌드, 테스트, 실행 로그와 게임 동작을 확인하면서 수정하는 방식으로 개발을
진행했습니다.

이 프로젝트의 소스와 문서는 이러한 AI 협업 개발 과정을 포함한 결과물입니다.

## Data and scientific scope

BANC v888 anatomy/connectivity is measured source data. The LIF dynamics,
sensory injection, actuator mapping, combat balance, rewards, and equipment
trade-offs are inferred or explicit FlyArena modeling/gameplay assumptions.
FlyArena is not a complete biological simulation of a living fruit fly.

Raw BANC data and generated caches are intentionally excluded from GitHub. See
`DATA_LICENSES.md` for the Nature paper, Harvard Dataverse citation, and CC BY
4.0 attribution.

## 라이선스와 2차 창작

FlyArena는 **오픈소스가 아니라 소스 공개형(source-available)** 프로젝트입니다.
개인 학습·교육·비상업 연구를 위해 읽고 빌드하고 비공개로 수정할 수 있지만,
수정판·모드·포트·후속작을 공개 배포하거나 상업적으로 사용하려면 사전 서면
허가가 필요합니다. 자세한 조건은 [LICENSE](LICENSE), 쉬운 한국어 설명은
[LICENSE_GUIDE_KO.md](LICENSE_GUIDE_KO.md), 허가 신청 방법은
[PERMISSION_REQUESTS.md](PERMISSION_REQUESTS.md)를 확인하세요.

BANC 데이터와 논문은 FlyArena 소유가 아니며 각 원 권리자의 조건을 그대로
따릅니다. [DATA_LICENSES.md](DATA_LICENSES.md)에 별도로 구분해 두었습니다.

## Public release status

현재 v0.6.8은 태그 기반 Source ZIP과 SHA-256 체크섬만 배포합니다. Windows
빌드·테스트·로컬 Defender 검사는 CI에서 계속 수행하지만, 서명되지 않은
실행 파일은 인터넷 다운로드 탐지가 해결될 때까지 CI artifact와 Release에
공개하지 않습니다. 관리자용 배포 절차는
[GitHub 공개·배포 안내](docs/GITHUB_RELEASE_KO.md)에 있습니다.
