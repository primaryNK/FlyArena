# FlyArena

FlyArena는 BANC v888/v3 초파리 연결망으로 두 개의 독립적인 신경 상태를
계산하고, 그 저수준 출력으로 물리 전투장을 움직이는 Windows 실험형
프로젝트입니다. 쉽게 말해, 미리 정해진 `공격()` 명령을 실행하는 게임이
아니라 신경 출력이 검·방패·이동 장치를 움직이고 충돌과 타이밍이 결과를
정합니다.

The last user-validated stable combat baseline is **v0.6.4d**. The current
experimental build is **v0.6.7**: full-loop speed control, compute-limited MAX
learning, swept sword collision, opposing spawns, and Flypack v4 customization.

## Current architecture

- BANC v888/v3: 188,508 neurons and 13,620,865 directed pairs
- two independent D3D12 neural-state sets at a 1 ms internal timestep
- sensory/world control at 50 ms
- physical combat substeps at at most 5 ms
- Win32 + Direct2D/DirectWrite renderer, decoupled from simulation
- low-level forward, turn, sword-drive, and shield-drive actuators only
- HIT/BLOCK/PARRY/DODGE classified from physical geometry and timing

The learner changes a small motor readout layer. It does not modify the BANC
connectivity pairs and does not add scripted actions such as `attack()` or
`parry()`.

## Build and verify

```bat
build_v067.bat
test_v067_profile_learning.bat
```

The portable tests cover flypack validation/round-trip, learning continuation
and freeze, equipment derivation, active-swing HIT, BLOCK/PARRY, unconditional
wall damage, stamina use, and 60-second HP-result rules.

The public tree keeps only the current v0.6.7 build/test entry points.

## 처음 사용하는 분

1. GitHub Releases에서 `FlyArena-v0.6.7-Windows-x64.zip`을 받습니다.
2. 압축을 풀고 동봉된 데이터 준비 배치를 실행합니다. BANC 원자료는
   크기와 별도 라이선스 때문에 ZIP에 포함되지 않습니다.
3. `FlyArena.exe`를 실행합니다.
4. `CREATE FLY`에서 초파리를 만들고 `TRAINING`으로 학습한 뒤 `BATTLE`로
   학습이 고정된 상태를 확인합니다.

설치, 데이터 준비, MAX 학습, 파일 종류와 문제 해결은
[한국어 시작 안내](docs/GETTING_STARTED_KO.md)에 정리했습니다.

## Run

- `run_v067_learning_arena.bat` — build and start in Training
- `run_v067_battle_arena.bat` — build and start in frozen Battle

The renderer provides:

- `CREATE FLY` — save a validated, data-only `.flypack`
- Create Fly has three linked gameplay controls: sword length, wing
  speed/endurance trade-off, and shield weight
- six physics-invariant silhouettes per body/equipment category and separate
  body/wing/sword/shield two-color gradients shared by preview and game
- `TRAINING` / `BATTLE` — cancel the current match and start the selected mode
- `PLAY` / `PAUSE`
- `REPEAT` — automatically start the next episode
- `RESET` — cancel and restart the current mode
- dotted HUD border — drop one `.flytrain`; the displayed `Save:` path becomes
  the active checkpoint for that slot
- `RESET THIS FLY LEARNING` — Training-only, confirmed reset of the displayed
  checkpoint

The first run loads and calibrates the CNS once. Repeated rounds reuse the same
resident topology, GPU resources, envelope, and evolving neural state. 1x–16x
pace the complete simulation tick. `MAX` removes wall-clock pacing, suppresses
per-tick visualization/CSV work, freezes the displayed flies at opposite spawn
points, and advances the hidden fight at the hardware limit. Simulation
timesteps do not change.

## Persistent data

- identity/equipment/cosmetics: `data\flies\*.flypack`
- learned plastic readout: `data\training\*.flytrain`
- telemetry and latest summaries: `results\`

Fly packages are data-only. Imported packages cannot load DLLs, scripts,
shaders, or arbitrary filesystem paths.

관심 있는 개발자와 연구자는 [문서 안내](docs/README.md)에서 현재 구조와
가정을 확인할 수 있습니다. 공개 Git과 소스 ZIP에는 현재 v0.6.7에 필요한
최종 파일만 들어가며, 개발 과정의 옛 빌드·실험 파일은 포함하지 않습니다.

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

Windows CI, 최종본 소스 스테이징, 태그 기반 실행 파일/소스 ZIP 패키징,
SHA-256 체크섬, 인용 정보와 이슈 양식이 준비되어 있습니다. 실제 공개 전에는
권리자 표기와 연락 수단을 확정하고 깨끗한 Windows 환경에서 최종 실행 검증이
필요합니다. 첫 push와 태그 배포 순서는
[GitHub 공개·배포 안내](docs/GITHUB_RELEASE_KO.md)를 따라가면 됩니다.
