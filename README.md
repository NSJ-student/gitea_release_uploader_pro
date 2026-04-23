# Gitea Release Uploader Pro

Qt Widgets 기반의 Gitea Release Asset 업로드 툴입니다.

## 주요 기능

- `.ui` 파일 기반 (Qt Designer 편집 가능)
- Gitea API 직접 호출
- Release Tag 조회
- Release 없으면 자동 생성
- 같은 이름 asset 자동 삭제 후 재업로드
- 업로드 진행률 표시
- 최근 입력값 저장(QSettings)
- Release 페이지 바로 열기

## 입력 예시

릴리즈 페이지가 아래라면:

`http://localhost:3000/admin/handbook/releases/tag/test`

각 항목은 이렇게 넣으면 됩니다.

- Gitea URL: `http://localhost:3000`
- Owner: `admin`
- Repo: `handbook`
- Tag: `test`

## 빌드

### qmake

```bash
qmake
make
```

### Qt Creator

- `gitea_release_uploader.pro` 열기
- `mainwindow.ui`는 Qt Designer에서 수정 가능

## 참고

- Token은 Gitea의 Personal Access Token을 사용하세요.
- 현재는 단일 파일 asset 업로드 기준입니다.
- Release 생성 시 body는 `Release Notes` 입력값을 사용합니다.
