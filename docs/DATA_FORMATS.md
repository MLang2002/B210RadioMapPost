# Data files and coordinate modes

This document explains the purpose of the files accepted and produced by B210RadioMapPost. It is
intended to help users choose the correct files and understand how radio measurements, building
geometry, and coordinate privacy relate to one another.

## Type-A: radio measurements and transmitters

A Type-A file represents the radio-data layer. Its ordinary rows describe RX measurement points;
TX locations are stored as TX records and appear in the same table and spatial view after import.

Typical Type-A information includes:

- point ID and RF sequence
- measurement-segment ID and timestamp
- latitude, longitude, and altitude
- East, North, and Up coordinates
- received power
- processing mode
- data origin, validity, and edited state
- RX/TX role in the application's in-memory model

RX points contribute to radio-map power aggregation. TX points are excluded from RX averaging and
are used when producing the transmitter mask.

Standard Type-A begins with a geographic ENU reference:

```text
#ENU_REF,<latitude_deg>,<longitude_deg>,<altitude_m>
```

The reference defines the origin and orientation used to relate WGS84 and ENU coordinates.

## Type-B: site and building geometry

A Type-B file represents the geometry layer. It contains two sections:

```text
#POINTS
...
#EDGES
...
```

The point section stores surveyed or edited vertices. The edge section stores pairs of point IDs
that connect those vertices. A rasterizable building normally consists of one or more closed,
non-self-intersecting loops.

Typical Type-B point information includes:

- point ID and timestamp
- latitude, longitude, and altitude
- East, North, and Up coordinates
- GNSS fix quality
- data origin and edited state

Standard Type-B also contains `#ENU_REF`. Type-A and Type-B can be displayed together after they
are resolved into one project frame.

## Raster Frame

A Raster Frame is a B210RadioMapPost extension of Type-B. It stores a copy of the building geometry
plus the information required to reproduce an aligned raster region:

- project origin and origin point ID
- left, right, bottom, and top ENU extents
- meters per pixel
- counter-clockwise rotation
- power minimum, maximum, and default value

Use a Raster Frame when several exports must share the same geometry, pixel scale, rotation, and
origin.

## Standard coordinate mode

Standard files contain both geographic and local coordinates:

- WGS84 latitude and longitude
- ellipsoidal altitude
- ENU East, North, and Up
- a geographic ENU reference

B210RadioMapPost checks that these values are complete and mutually consistent. Editing WGS84
updates ENU; editing ENU updates WGS84 in the active project frame.

Standard data should be used when real geographic positioning is required and the geographic
coordinates are permitted to remain in the exported files.

## Private ENU coordinate mode

Private files are intended for workflows in which geographic locations must not be distributed.
They retain the local geometry but remove:

- point latitude, longitude, and altitude
- `#ENU_REF`
- geographic project-origin records

Required private metadata includes:

```text
#LOCAL_FRAME_ID,<identifier>
#COORDINATE_AUTHORITY,ENU
#GEOGRAPHIC_FIELDS,REMOVED
#AXIS,EAST,NORTH,UP
#UNIT,METER
```

The geographic cells are displayed as `Hidden` and are read-only. ENU remains editable and is the
sole coordinate authority.

Private Type-A and Type-B can be combined only when their `LOCAL_FRAME_ID` values match. The
identifier establishes frame compatibility; it does not reveal the geographic origin.

Changing the Fallback Geographic Anchor in Settings does not reproject or modify Private ENU data.

## Private file variants

The supported private markers are:

```text
#B210POST_PRIVATE_A,1
#B210POST_PRIVATE_A,2
#B210POST_PRIVATE_B,1
#B210POST_PRIVATE_RASTER_FRAME,1
#B210POST_PRIVATE_RASTER_FRAME,2
```

Private Type-A v2 preserves local-interpolation provenance such as generation kind, interpolation
batch ID, and interpolation method. Standard Type-A does not have equivalent fields, so this
metadata is omitted when a Synthetic WGS84 standard copy is created.

Raster Frame v2 adds raster rotation; v1 is interpreted with zero rotation.

## Synthetic WGS84 coordinate mode

Synthetic WGS84 files are standard-format copies generated from:

1. the ENU coordinates in matching Private Type-A and Type-B files; and
2. the Fallback Geographic Anchor supplied by the user.

The export command is enabled only when:

- both datasets contain points;
- both datasets are Private ENU;
- both have the same non-empty `LOCAL_FRAME_ID`.

The conversion produces WGS84 coordinates for every point, verifies their consistency with the
source ENU coordinates, and writes standard files. The in-memory Private datasets are unchanged.

Synthetic WGS84 describes an assigned geographic placement. It is not measured GNSS data, and its
accuracy cannot exceed the accuracy of the anchor and the source local frame.

## Compatibility summary

| Type-A | Type-B | Compatible | Reason |
| --- | --- | :---: | --- |
| Standard | Standard | Yes | Geographic relationship can be resolved |
| Private | Private, same frame ID | Yes | Shared local frame is identified |
| Private | Private, different frame ID | No | Relative relationship is unknown |
| Standard | Private | No | Absolute relationship cannot be proven |
| Private | Standard | No | Absolute relationship cannot be proven |

## Source-file safety

Imported files are parsed into memory and then closed. Their normalized paths are protected from
Save/Export operations. Choose a different output path when saving edited or converted data.

