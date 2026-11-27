# User guide

This guide describes the main B210RadioMapPost workflow. The interface contains additional options
for specialized tasks; hover over controls to read their tooltips before using them.

## 1. Configure the fallback anchor

Open **Settings** to configure the Fallback Geographic Anchor:

- reference latitude
- reference longitude
- reference ellipsoidal altitude

The anchor is used for new data without a file reference, raw recovery fallback behavior, and
Synthetic WGS84 export. It does not change the geometry of loaded Private ENU files.

Settings also contains basic canvas-display options such as point size and inactive-layer opacity.

## 2. Import data

Select **File > Import** and choose a Type-A file. A compatible Type-B or Raster Frame can be loaded
at the same time.

After import, check the status area at the bottom of the window. It identifies the active coordinate
state, including:

- Original/file WGS84
- Private ENU and its frame ID
- Synthetic WGS84 based on the Settings anchor

The status area also reports RX/TX counts, visible points, building points and edges, default power,
and unsaved changes.

## 3. Inspect and select points

The map and tables share their selection state:

- select table rows to highlight map points;
- select map points to locate their table rows;
- use Ctrl/Shift selection for multiple points;
- use the visibility checkbox to hide a marker from the interactive view.

Visibility is a session-only display property. It does not remove a point or exclude it from raster
calculations.

## 4. Edit radio data

Choose **Edit Power / GNSS**.

Standard data allows edits to WGS84, ENU, and power values. Coordinate edits update the companion
coordinate representation automatically. Private data keeps WGS84 cells hidden and uses ENU as the
editable coordinate authority.

Use **Toggle RX / TX** to change the role of selected Type-A points. RX points contribute to the
radio map; TX points contribute to the transmitter mask.

Use **Place Point** to add a manual point at a selected ENU location. The point receives the current
default power and can be edited afterward.

## 5. Edit building geometry

Choose **Edit Building GNSS**.

You can place vertices, connect selected vertices, delete edges, or remove selected points. A valid
raster building is composed of closed loops.

Open **Building Shapes...** for parameterized geometry:

- rectangle center, width, height, and rotation
- circle center and radius
- independent edge-discretization settings
- shape copying and placement
- temporary horizontal and vertical rulers

The generated contours are validated before they replace or extend the active geometry.

## 6. Fill local measurement gaps

In **Edit Power / GNSS** mode, open **Local Fill...** and select a rectangular ENU region.

Available point-generation methods include:

- Local IDW
- Local Ordinary Kriging

Configure the method, preview the proposed points, and confirm only when the result is acceptable.
Generated points are marked as interpolated and are not reused as source measurements in later
local-fill operations.

## 7. Configure the raster region

The Raster / Export Region controls:

- left, right, bottom, and top distances from the ENU origin
- meters per pixel
- counter-clockwise rotation
- power display range and default power
- color mode and presentation options

Use **LOAD FRAME** to load a Type-B or Raster Frame. A valid held frame keeps geometry and export
alignment stable while compatible Type-A files are processed.

## 8. Render and inspect

Choose one of the raster products:

- Building Mask
- TX Mask
- Radio Map
- Sample Site Mask

Select **Render Preview** to inspect the result before saving. The radio-map preview can identify
missing free-space cells, suggest local values, complete gaps, and apply assisted or manual pixel
overrides.

These preview operations do not create Type-A rows and do not change the physical sample count.

Saved images can include aligned JSON sidecars and optional presentation companions such as an
alternate grayscale/RGB image or color bar.

## 9. Save or convert data

The **File > Save** submenu contains standard Type-A/Type-B saving and Synthetic WGS84 export.

### Save standard data

Use **Save Type-A** or **Save Type-B** for ordinary standard-coordinate datasets. Imported source
paths cannot be overwritten.

### Create Private ENU copies

Use **File > Create Private Copy...** and choose an output directory. The application removes
geographic coordinates and writes automatically named private files while preserving ENU geometry.

### Export Synthetic WGS84 copies

Load matching Private Type-A and Type-B files, verify the Fallback Geographic Anchor, and select:

```text
File > Save > Export Synthetic WGS84 Copies...
```

Choose an output directory. The application creates standard-coordinate copies and includes a
Raster Frame when one is currently held. Existing files with the generated names require explicit
replacement confirmation.

The loaded Private ENU data remains active and unchanged after export.

## 10. Recover a raw timeline [deprecated]

Open **Tools > Raw Recovery**, select a supported raw timeline, inspect the confirmed recovered RX
records, and save them to a new Type-A file.

Only accepted RF records with a usable GNSS time bracket are recovered. TX locations cannot be
reconstructed from the raw timeline.

