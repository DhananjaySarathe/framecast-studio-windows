# Windows studio implementation reference

This surface extends the existing Mac visual system; no replacement identity is introduced.

Mode: Operate. Match `Sources/Framecast/Theme.swift`, `StudioView.swift`, and `ProgramView.swift`. Use the current source's geometry and nine inspector panels; committed screenshots provide composition and density evidence.

Surfaces: window `#0E1015`, body `#0B0D11`, program well `#07080B`, scenes `#101318`, inspector `#12151B`, controls `#1A1E27`, selected `#262C3A`, field `#0C0E13`, hairline `#232733`. Text `#EEF0F6`, secondary `#A8AFBF`, muted `#7F8798`. Accent `#7D6BFF`, recording `#FF4D5E`, healthy `#3DD68C`, warning `#F5B544`.

Use the platform system sans face, with a 12 px control body, 14 px section headers, and 10–11 px supporting labels. Use tabular time readouts. Authored line icons use one consistent stroke weight. Tooltips, accessible names, visible keyboard focus and standard Tab order supplement compact controls.

At 1440 px, keep scenes 236 px and inspector 340 px with the center expanding. The toolbar is 52 px and bottom transport 64 px. At 1280 px, preserve all transport actions without overlapping status; windows have a 1180 px minimum width. Startup height is clamped to the current screen work area with a 620 px minimum; inspector pages scroll when their controls do not fit. Canvas images retain their own aspect ratio; selected-canvas violet outline indicates which orientation scene changes affect. Preview mode shows its independent candidate and a visible Put on air action.

The first viewport contains real engine previews, scene thumbnails, the selected inspector panel, and independent Record / Start broadcast controls. Never populate fake comments, guest identities or broadcast statistics. Unsupported panels state their current availability. Source errors, pending output finalization and reconnecting destinations remain visible.

Signature interaction: select a scene privately in Preview, inspect both layouts, then Put on air without restarting capture or disrupting outputs. The UI reads a bounded frame mailbox and sends thread-safe scene/output/gain commands to the same engine used by the CLI.
