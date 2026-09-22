FlyArena 밖의 데이터 경로를 참조하지 않습니다.

Windows Release ZIP에는 대용량 BANC 원자료/캐시가 포함되지 않습니다.
압축을 푼 루트에서 `SETUP_DATA_AND_RUN.bat`를 최초 한 번 실행하면
`data/cache`와 `data/io`를 로컬에 준비하고 FlyArena를 시작합니다.

`flies/` contains versioned, data-only `.flypack` character profiles.
`training/` contains local `.flytrain` learned-readout checkpoints and is not
trusted through arbitrary paths supplied by a package.
