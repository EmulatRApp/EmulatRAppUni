# EmulatR Help & Manual (H&M) Documentation Formatting Rule

When authoring or converting EmulatR documentation into Help & Manual
topic XML, reproduce the visual differentiation that rendered Markdown
provides. Highlight/typeface differentiation materially improves
readability, so we map every Markdown construct to its H&M equivalent
rather than flattening to `Normal` text. ASCII-only (project rule).

## Construct mapping (Markdown -> H&M XML)

| Markdown | H&M XML |
|---|---|
| `# Heading` | `<para styleclass="Heading1">...</para>` |
| `## Heading` | `<para styleclass="Heading2">...</para>` |
| `### Heading` | `<para styleclass="Heading3">...</para>` |
| body paragraph | `<para styleclass="Normal">...</para>` |
| `**bold**` | `<text style="font-weight:bold;">...</text>` |
| `*italic*` / `_italic_` | `<text style="font-style:italic;">...</text>` |
| inline `` `code` `` | `<text styleclass="Code Example">...</text>` |
| fenced ```` ``` ```` code block | one `<para styleclass="Code Example">` per line; drop the ```` ```lang ```` fence line |
| `---` (horizontal rule) | `<para styleclass="Normal"><line style="height:1px; color:#000000;" /></para>` |
| `\| pipe table \|` | `<table styleclass="Default" rowcount="R" colcount="C" style="...; cell-border-width:1px; ...; head-row-background-color:#00ffff;">` |
| `- list item` | `<para styleclass="Normal">- ...</para>` (literal bullet) |
| blank line | `<para styleclass="Normal"></para>` |

## Table rules

- `rowcount` INCLUDES the header row (1 header + N data = N+1).
- Header cells use `<thead>` with each `<td><para styleclass="Normal"><text style="font-weight:bold;">Label</text></para></td>`.
- Data cells: `<td><para styleclass="Normal">...</para></td>`.
- Header-row background is `#00ffff` (cyan) per house style.
- Apply the inline conventions below INSIDE cells too.

## Inline conventions (the readability layer)

Identifiers are rendered monospace via the `Code Example` styleclass even
where the Markdown source left them as plain text inside tables -- this is
a deliberate enrichment for readability, not a faithful round-trip:

- env-var names, CLI flags, file paths, function/symbol names, PA/hex
  addresses, and literal code tokens -> `<text styleclass="Code Example">`
- first/emphatic use of a key term -> `<text style="font-weight:bold;">`
- definitional or cautionary asides -> `<text style="font-style:italic;">`

## Escaping

- `<` -> `&lt;`   `>` -> `&gt;`   `&` -> `&amp;`   `"` -> `&quot;`
- non-breaking space -> `&#160;`   leading hard space -> `&#32;`
- Apply escaping to text content only, never to tag/attribute syntax.

## Worked example

Markdown:

```
| Variable | Effect |
|---|---|
| `EMULATR_NO_PUTTY` | Do not auto-launch PuTTY; the listen socket still comes up. |
```

H&M XML:

```
<table styleclass="Default" rowcount="2" colcount="2" style="border-width:0px; border-spacing:0px; border-collapse:collapse; cell-border-width:1px; border-color:#000000; border-style:solid; head-row-background-color:#00ffff; alt-row-background-color:none;">
  <thead style="vertical-align:top">
    <td><para styleclass="Normal"><text style="font-weight:bold;">Variable</text></para></td>
    <td><para styleclass="Normal"><text style="font-weight:bold;">Effect</text></para></td>
  </thead>
  <tr style="vertical-align:top">
    <td><para styleclass="Normal"><text styleclass="Code Example">EMULATR_NO_PUTTY</text></para></td>
    <td><para styleclass="Normal">Do not auto-launch PuTTY; the listen socket still comes up.</para></td>
  </tr>
</table>
```

`ENV_VARS.xml` in this directory is the reference implementation of this rule.
