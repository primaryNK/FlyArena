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
experimental build is **v0.6.8**: one-click Release data setup, full-loop speed control, compute-limited MAX
learning, canonical sword reach, body/wing hitboxes, opposing spawns, and Flypack v4 customization.

## Current architecture

* BANC v888/v3: 188,508 neurons and 13,620,865 directed pairs
* two independent D3D12 neural-state sets at a 1 ms internal timestep
* sensory/world control at 50 ms
* physical combat substeps at at most 5 ms
* Win32 + Direct2D/DirectWrite renderer, decoupled from simulation
* low-level forward, turn, sword-drive, and shield-drive actuators only
* HIT/BLOCK/PARRY/DODGE classified from physical geometry and timing

The learner changes a small motor readout layer. It does not modify the BANC
connectivity pairs and does not add scripted actions such as `attack()` or
`parry()`.

## Build and verify

```bat
build_v068.bat
test_v068_profile_learning.bat
```

The portable tests cover flypack validation/round-trip, learning continuation
and freeze, equipment derivation, active-swing HIT, BLOCK/PARRY, unconditional
wall damage, stamina use, and 60-second HP-result rules.

The public tree keeps only the current v0.6.8 build/test entry points.

## 처음 사용하는 분

> **Windows 실행 파일 배포 일시 중단:** 현재 서명되지 않은 v0.6.8 실행
> 파일은 로컬 및 GitHub Actions의 Microsoft Defender 검사를 통과하지만,
> 인터넷에서 내려받을 때 Defender 클라우드 검사에서
> `Trojan:Win32/Wacatac.B!ml`로 탐지됩니다. 현재 조사 결과로는 오탐이
> 의심되지만 Microsoft 분석 전에는 확정할 수 없습니다. 경고를 무시하거나
> Defender를 끄지 마세요. Microsoft가 탐지를 해제하거나, 신뢰할 수 있는
> Authenticode 서명을 적용한 대체 빌드가 인터넷 다운로드 검사를 통과할
> 때까지 Release에는 검증 가능한 소스 ZIP만 제공합니다.

개발자는 아래 빌드 명령으로 소스에서 직접 빌드할 수 있습니다. 일반 사용자는
Microsoft 분석과 인터넷 다운로드 검사를 통과한 Windows 바이너리가 다시
게시될 때까지 기다려 주세요. Windows ZIP이 복구된 뒤에는 압축을 풀어
`SETUP_DATA_AND_RUN.bat`를 최초 한 번 실행하고,
이후 `START_FLYARENA.bat`를 사용하면 됩니다.

설치, 데이터 준비, MAX 학습, 파일 종류와 문제 해결은
[한국어 시작 안내](docs/GETTING_STARTED_KO.md)에 정리했습니다.

## Run

* `run_v068_learning_arena.bat` — build and start in Training
* `run_v068_battle_arena.bat` — build and start in frozen Battle

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

Windows 빌드·테스트·로컬 Defender 검사, 최종본 소스 스테이징, 태그 기반
소스 ZIP, SHA-256 체크섬, 인용 정보와 이슈 양식이 준비되어 있습니다. 현재
서명되지 않은 실행 파일은 Defender의 인터넷 다운로드 탐지가 해결될 때까지
CI artifact와 Release에서 공개하지 않습니다. 첫 push와 태그 배포 순서는
[GitHub 공개·배포 안내](docs/GITHUB_RELEASE_KO.md)를 따라가면 됩니다.
