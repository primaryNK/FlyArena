# FlyArena GitHub 공개·배포 안내

현재 저장소는 `main` 브랜치와 최종본 공개 허용 목록이 준비되어 있습니다.
아래 명령은 실제로 외부에 공개하므로, 권리자 표기와 공개 파일 목록을 먼저
확인한 뒤 프로젝트 소유자가 직접 실행하세요.

## 1. 공개 전 최종 확인

```powershell
git status --short --untracked-files=all
git ls-files --others --exclude-standard
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\stage_public_source.ps1 -Version v0.6.8
```

- `LICENSE`의 `FlyArena Project Owner`를 실제 권리자 이름/조직명으로 변경
- `PUBLIC_SOURCE_MANIFEST.txt`와 Git 공개 파일이 일치하는지 확인
- BANC 원자료, 캐시, `.flytrain`, `results`, 비밀번호나 토큰이 없는지 확인
- `build_v068.bat`과 `test_v068_profile_learning.bat`이 통과하는지 확인

## 2. 첫 로컬 커밋

Git 작성자 정보가 없다면 먼저 설정합니다.

```powershell
git config user.name "YOUR NAME"
git config user.email "YOUR_GITHUB_EMAIL"
git add --all
git status --short
git commit -m "Release FlyArena v0.6.8"
```

`git add --all`은 `.gitignore`를 따르지만, 커밋 전 `git status`에서 최종
목록을 반드시 눈으로 확인하세요.

## 3. GitHub CLI로 공개 저장소 만들기

GitHub CLI를 설치하고 인증합니다.

```powershell
gh auth login
gh repo create OWNER/FlyArena --public --source=. --remote=origin --push
```

`OWNER`는 GitHub 사용자명 또는 조직명으로 바꿉니다. 기존 로컬 저장소를
올릴 때 GitHub 웹에서 README/라이선스가 자동 생성된 별도 초기 커밋을 먼저
만들지 않는 편이 충돌을 피하기 쉽습니다.

웹에서 빈 저장소를 먼저 만들었다면 다음 방식도 가능합니다.

```powershell
git remote add origin https://github.com/OWNER/FlyArena.git
git remote -v
git push -u origin main
```

## 4. v0.6.8 자동 Release 만들기

`main`의 CI가 성공한 것을 확인한 뒤 서명 설명 태그를 push합니다.

```powershell
git tag -a v0.6.8 -m "FlyArena v0.6.8"
git push origin v0.6.8
```

`.github/workflows/release.yml`이 Windows에서 다시 빌드·테스트하고 다음을
GitHub Release에 첨부합니다.

- `FlyArena-v0.6.8-Windows-x64.zip`
- `FlyArena-v0.6.8-Source.zip`
- `SHA256SUMS.txt`

GitHub 저장소의 **Actions** 탭에서 `Tagged Windows Release`가 성공했는지
확인하고, **Releases**에서 세 파일과 체크섬을 확인합니다. 태그를 push하기
전에 버전 파일을 커밋해야 하며, 동일한 공개 태그를 나중에 다른 커밋으로
옮기지 않는 것이 좋습니다.

## 5. 수동 배포가 필요할 때

워크플로가 아닌 로컬에서 ZIP을 만들 수 있습니다.

```powershell
.\build_v068.bat
.\test_v068_profile_learning.bat
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\package_release.ps1 -Version v0.6.8
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\stage_public_source.ps1 -Version v0.6.8
```

생성된 `dist`의 ZIP 두 개와 `SHA256SUMS.txt`를 GitHub Release에 첨부합니다.
Release는 Git 태그가 가리키는 소스 시점의 배포판이므로, 로컬 수정본을
커밋하지 않은 채 다른 바이너리만 올리지 마세요.
