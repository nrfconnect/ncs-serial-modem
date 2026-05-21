---
applyTo: "doc/**"
---
# Documentation Guidelines

Reference: [Nordic Tech Docs Style Guide](https://nordicplayground.github.io/techdocs-style-guide/)

## reStructuredText Standards
- Write valid reStructuredText code
- Follow proper RST syntax and formatting
- Use appropriate directives and roles

## Style Guidelines
- Maintain consistent formatting and structure
- Use American English spelling (reference: [Merriam-Webster](https://www.merriam-webster.com/))
- Use present tense
- Use active voice; passive voice is acceptable only when the system performs the action, the receiver of the action should be emphasized, or the passive form is clearer
- Use gender-neutral language
- Keep sentences short; use subject + verb + object order
- Use lists where appropriate
- Avoid nominalizations, phrasal verbs, and Latin abbreviations (e.g., use "before" instead of "prior to", "help" instead of "assist")
- Do not use slang, cultural allusions, jokes, or ambiguous phrases
- Do not attribute human qualities to hardware or software

## Capitalization
- Document titles: Title Case
- Headings: Sentence case

## Punctuation
- Use the Oxford comma
- Use standard American punctuation

## Numbers and Units
- Always use the abbreviated unit form (e.g., "7 ms"), except when a unit is used without a value — then spell it out (e.g., "dimensions in millimeters")
- Use a comma as the digit-group separator for numbers over four digits: `50,000` not `50 000`
- For ranges with units, use "to" instead of a dash to avoid ambiguity with negative numbers (e.g., `-40°C to 100°C`)
- Spell out ordinal numbers in text: first, tenth, thirty-third

## Graphic Elements
- Every figure must have an informative caption
- Descriptive text or an introduction must appear **before** the figure, not after
- Use the SVG Diagram Guidelines (below) when creating new figures

## File Templates
- For new documentation files, use nRF template: [templates.rst](https://github.com/nrfconnect/sdk-nrf/blob/main/doc/nrf/dev_model_and_contributions/documentation/templates.rst)
- Follow established documentation patterns
- Include proper metadata and headers

## Best Practices
- Write clear, actionable documentation
- Include code examples where appropriate
- Keep documentation up-to-date with code changes

## SVG Diagram Guidelines

### File format and size

- Use **SVG** for diagrams and figures; use **PNG** for software screenshots (windows, UI snippets).
- Maximum size for nRF Connect SDK docs: width 780 px, height 800 px.
- Keep the SVG file size under 1 MB.

### Color palette

Use only the following Nordic color palette (black and white are also allowed in text):

**Primary colors:**
- Nordic Blue: RGB 0 162 198 (`#00A2C6`)
- Nordic Sky: RGB 106 209 227 (`#6AD1E3`)
- Nordic Lake: RGB 0 119 200 (`#0077C8`)
- Nordic Blueslate: RGB 0 50 160 (`#0032A0`)

**Supporting colors:**
- Nordic Pink: RGB 198 0 126 (`#C6007E`)
- Nordic Red: RGB 209 49 79 (`#D1314F`)
- Nordic Fall: RGB 222 130 59 (`#DE823B`)
- Nordic Sun: RGB 224 203 67 (`#E0CB43`)
- Nordic Grass: RGB 206 221 80 (`#CEDD50`)
- Nordic Power: RGB 139 192 88 (`#8BC058`)

**Neutral colors:**
- Nordic Dark Grey: RGB 51 63 72 (`#333F48`)
- Nordic Middle Grey: RGB 118 134 146 (`#768692`)
- Nordic Light Grey: RGB 217 225 226 (`#D9E1E2`)

### Shapes

- Do not use border lines in blocks, ellipses, circles, etc.
- Do not add any effects (shadow, glow, gradient, rounded corners, etc.).
- Do not use colors outside the Nordic color palette.

### Layout

- Always align shapes (top/bottom/left/right/center/middle).
- Distribute shapes horizontally/vertically for a balanced look.
- Data flow direction: left to right.
- Tasks and events direction: top-down. Tasks enter a block from above; events exit downward.

### Text

- Font: **Arial**, style: Regular.
- Do not use bold or italic font.
- Do not distort text (stretch, flatten, etc.).
- Use font size or block size to indicate hierarchy — not bold.
- Font color: white on dark backgrounds, black on light backgrounds. Ensure sufficient contrast.
- Connector label color should match the color of the connector line.
- Capitalization: sentence case.
- Vertical text direction: top-down.

### Connectors and arrows

- Use simple open or filled arrowheads. Avoid decorative, doubled, or overly stylized arrow ends.
- Arrowhead size: medium as a default; scale down proportionally if shapes are small.
- Place connector label text above the connector line.
- Connector labels must have a transparent background — no white or colored fill behind the text.

### Accessibility

- Do not rely on color alone to convey meaning. For color-blind users, add text labels or other visual indicators when shapes of different categories would otherwise look identical.
- Use different colors to differentiate states or sections of the diagram where applicable.
