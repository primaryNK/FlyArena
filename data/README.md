FlyArena 밖의 데이터 경로를 참조하지 않습니다.

`flies/` contains versioned, data-only `.flypack` character profiles.
`training/` contains local `.flytrain` learned-readout checkpoints and is not
trusted through arbitrary paths supplied by a package.
