# FileFind - Strings + Pattern Matching

FileFind is a C++17 full-stack academic project for uploading documents, extracting readable text, and searching contents with classic string matching algorithms.

## Objectives

- Demonstrate file handling, text processing, REST APIs, and client-server architecture.
- Implement Naive, KMP, Rabin-Karp, Boyer-Moore, wildcard pattern, and regex search.
- Support uploaded PDF, DOCX, PPTX, and TXT files from the current user only.

## Features

- Multi-file upload with validation and 5 GB per-file limit
- Dashboard statistics and recent searches
- File management with delete and clear-all actions
- Case-sensitive or case-insensitive search
- File type filters and selected-file search
- Match highlighting and surrounding context
- Algorithm comparison with a small JavaScript chart
- Per-user JSON metadata and file storage under `FILE_STORAGE_PATH`

## Technology Stack

- Backend: C++17, CMake, standard library, Windows Winsock or POSIX sockets
- Frontend: HTML5, CSS3, JavaScript Fetch API
- Optional extraction tools: Poppler `pdftotext` for high-quality PDF extraction
- DOCX/PPTX: C++ backend invokes a PowerShell unzip helper on Windows and parses XML text nodes; on other systems it tries `unzip -p`

## System Architecture

```text
Browser
  -> REST API
C++ HTTP server
  -> upload validation
  -> text extraction
  -> authenticated per-user persistent storage
  -> pattern matching algorithms
  -> JSON responses
Frontend renders dashboard, files, search results, and comparison chart
```

## Folder Structure

```text
backend/
  main.cpp
  search/SearchAlgorithms.h
  extraction/TextExtractor.h
  models/FileInfo.h
  utils/Json.h
frontend/
  index.html
  style.css
  script.js
data/users/<user-id>/
  files/
  extracted_text/
  data/files.db
workspaces/ (legacy per-user migration source)
samples/
CMakeLists.txt
```

## Installation Requirements

- CMake 3.16+
- A C++17 compiler
  - Windows: MSVC Build Tools or MinGW
  - Linux/macOS: GCC or Clang
- Linux runtime: `unzip` for DOCX/PPTX extraction; Poppler `pdftotext` is optional for higher-quality PDF extraction

## Build Instructions

```bash
cmake -S . -B build
cmake --build build --config Release
```

## Run Instructions

```bash
./build/FileFind
```

On Windows with Visual Studio generators:

```powershell
.\build\Release\FileFind.exe
```

Open:

```text
http://localhost:8080
```

Always open the app through this address. Opening `frontend/index.html` directly from the file system cannot reach the C++ API, so uploads and searches will not work.

## Windows File Access and Packaging

FileFind does not scan another person’s computer without consent. The user grants access by choosing files or a folder in the Windows File Explorer picker. The app receives only the selected supported documents and stores them in that authenticated user’s storage directory.

To create a portable install directory after building:

```powershell
cmake --install build --prefix package
```

Copy the generated `package` folder to another Windows computer. Run `FileFind.exe` from that folder, then open `http://localhost:8080`. The user can choose **Choose files** or **Choose folder** and Windows will control the access boundary.

### Windows Explorer APIs

- `POST /api/explorer/select-folder` opens the native Windows folder dialog.
- `POST /api/explorer/select-files` opens the native multi-file dialog.
- `POST /api/explorer/scan` scans the selected folder, optionally recursively.
- `POST /api/search/local` searches the selected local documents with the existing algorithms.
- `POST /api/explorer/open-file` opens a selected result in its default Windows application.
- `POST /api/explorer/show-in-explorer` highlights a selected result in File Explorer.

The server listens on all network interfaces and uses the `PORT` environment variable, defaulting to `8080`. It never scans the computer automatically, and local open/show actions reject paths that were not selected through File Explorer. Native file/folder dialogs and Windows Explorer actions are unavailable on Linux cloud deployments; browser uploads and normal file operations remain available.

Cloud hosts may use ephemeral storage by default. Set `FILE_STORAGE_PATH` to a mounted persistent disk to keep uploaded files and account data across restarts and redeployments.

### Persistent User Storage

FileFind selects the storage root from `FILE_STORAGE_PATH`. If the variable is unset, local development uses `data/users/` beneath the application directory. Each authenticated user is stored separately:

```text
data/users/<user-id>/files/
data/users/<user-id>/extracted_text/
data/users/<user-id>/data/files.db
```

The account and session databases (`users.db` and `sessions.db`) live at the storage root. The backend derives the user ID from the existing bearer-token session; workspace headers and request bodies cannot select another user's directory.

For Render, create a persistent disk mounted at `/var/data` and set this service environment variable:

```text
FILE_STORAGE_PATH=/var/data/users
```

The resulting user files are stored under `/var/data/users/<user-id>/`. Do not put this path in source code or commit secrets. Without a mounted persistent disk, container-local files can be lost during restarts or redeploys.

On startup, FileFind copies legacy `data/users.db`, `data/sessions.db`, and an authenticated user's existing `workspaces/u_<user-id>/` data into the selected storage root when the destination is missing. It does not delete the old data. Legacy files in an unauthenticated `workspaces/default/` cannot be safely assigned to an account, and data that was already lost with an ephemeral Render container cannot be recovered by this migration.

## API Documentation

### `POST /api/upload`

Multipart form upload. Field name: `files`.

### `GET /api/files`

Returns uploaded file metadata.

### `DELETE /api/files/{id}`

Deletes one uploaded file and its extracted text.

### `DELETE /api/files`

Clears all uploaded files.

### `PATCH /api/files/{id}`

Renames a file without changing its contents. Send `{ "name": "new-name.txt" }` and keep the original extension.

### `GET /api/files/{id}/open`

Opens a stored file inline when the browser supports its format.

### `GET /api/files/{id}/download`

Downloads the stored file using its current display name.

### `POST /api/search`

Request:

```json
{
  "query": "database",
  "algorithm": "kmp",
  "mode": "simple",
  "caseSensitive": false,
  "fileTypes": ["pdf", "docx", "pptx", "txt"],
  "fileIds": []
}
```

### `POST /api/compare`

Runs Naive, KMP, Rabin-Karp, and Boyer-Moore for the same query.

### `GET /api/stats`

Returns dashboard totals and recent searches.

## Supported File Types

- `.txt`: direct UTF-8/ASCII text read
- `.pdf`: uses `pdftotext` when available; otherwise a conservative byte-string fallback
- `.docx`: extracts XML from `word/document.xml`
- `.pptx`: extracts slide XML from `ppt/slides/slide*.xml`

## Search Algorithms

| Algorithm | Time Complexity | Space Complexity |
| --- | --- | --- |
| Naive | O(n * m) | O(1) |
| KMP | O(n + m) | O(m) |
| Rabin-Karp | Average O(n + m), worst O(n * m) | O(1) |
| Boyer-Moore | Best sublinear, worst O(n * m) | O(k) |
| Regex | Engine dependent | Engine dependent |

## Screenshots

Run the app and add screenshots of the dashboard, upload area, search results, and comparison page here.

## Future Enhancements

- SQLite persistence
- Authentication and per-user storage
- Streaming uploads
- Better PDF extraction with Poppler linked directly
- Full-text indexing for very large document sets
- Exportable search reports
