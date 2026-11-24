# `.uvproject` & Template Contract (v1)

## 1. Project Manifest (`*.uvproject`)

Every UniVex project has a JSON manifest with the `.uvproject` extension at its root:

```json
{
  "format": "uvproject",
  "schemaVersion": 1,
  "revision": 1,
  "projectId": "0123456789abcdef",
  "displayName": "My Game",
  "engineVersion": {
    "major": 0,
    "minor": 1,
    "patch": 2,
    "build": 0
  },
  "rendererProfile": "forward_plus",
  "contentRoot": "Content",
  "assetDatabasePath": ".uvassetdb",
  "settingsPath": "project.uvsettings",
  "startupScenePath": "Scenes/Main.uvscene"
}
```

### Required & Standard Fields

- `format`: `"uvproject"`
- `schemaVersion`: `1`
- `revision`: positive monotonic integer (`>= 1`)
- `projectId`: non-empty stable identifier
- `displayName`: human-readable project name
- `engineVersion`: object `{ major, minor, patch, build }` (unsigned 32-bit integers)
- `rendererProfile`: serialized identifier `"compatibility"`, `"mobile"`, or `"forward_plus"` (UI labels: `Compatibility`, `Mobile`, `Forward+`; optional for older manifests, defaulting to `"mobile"` on Android and `"forward_plus"` on desktop)
- `contentRoot`: project-relative directory path (typically `"Content"`)
- `assetDatabasePath`: project-relative filename (typically `".uvassetdb"`)
- `settingsPath`: project-relative filename (typically `"project.uvsettings"`)
- `startupScenePath`: content-relative startup scene path, or empty string `""` when not yet configured
- Optional blocks: `productMetadata` (`EditorProjectProductMetadataUVE`) and `applicationSettings` (`EditorProjectApplicationSettingsUVE`), preserved round-trip without data loss.

## 2. Project Templates (`template.json` & `starter_scene.uvstarter`)

Project templates shipped with the Hub live under `project_templates/<TemplateName>/template.json`:

- `format`: `"uvtemplate"`, `schemaVersion`: `1`
- Declares `id`, `displayName`, `description`, optional `contentDirectory`, optional `startupScenePath`, and optional `starterScene.objects[]` (up to 256 objects with `kind`, `name`, `position`, `rotationDegrees`, `scale`).
- When a template declares a `starterScene`, the Hub writes `starter_scene.uvstarter` (`format: "uvstarter"`, `schemaVersion: 1`) at the root of the newly created project. The Engine Editor builds the initial scene from it on first open and removes the file.

## 3. Version Compatibility

- A project created with engine version `(M, m, p, b)` can be opened by an installed engine with matching major version and `installed >= project` (exact match preferred, then newest compatible installed engine).
- Major version mismatches or older installed engines are rejected or flagged clearly before launch.
