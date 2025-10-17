# Archive - Historical Analysis Data

## ⚠️ Important Note

**You don't need to read these files to implement the driver.**

All critical information has been consolidated into:
- **`../MASTER_IMPLEMENTATION_GUIDE.md`**

This archive preserves the research trail and raw data for:
- Verification and validation
- Understanding analysis methodology
- Reproducing research results
- Deep-dive protocol investigation

---

## What's in the Archive

### `analysis/` - Individual Function Analyses
45+ markdown files documenting specific functions from the decompiled binaries.

**Format:** `function_<Name>_<Address>.md`

**Examples:**
- `function_SetPeriodic_18000cbbc.md` - Periodic effect encoding
- `function_SetConstant_18001d4f8.md` - Constant force encoding
- `function_SetEnvelope_18000d3f4.md` - Envelope application
- `function_SetCondition_18001b6dc.md` - Condition effects (spring/damper)

**Use Case:** Verifying specific protocol details or implementation choices.

---

### `findings/` - Raw Analysis Outputs
Intermediate results and summaries from analysis sessions.

**Contains:**
- JSON summaries of automated analysis
- Production analysis data
- Effect parameter analysis
- Cross-reference tables

**Use Case:** Raw data for statistical analysis or protocol reconstruction.

---

### `automated_mcp_results/` - Automated MCP Analysis
Results from automated MCP-assisted decompilation runs.

**Contains:**
- Bulk function analysis outputs
- String extraction results
- Cross-reference data
- Automated pattern detection

**Use Case:** Understanding automated analysis workflow and results.

---

### `comprehensive_analysis_results/` - Multi-Binary Analysis
Comprehensive analysis of all 8 binaries together.

**Contains:**
- Cross-binary function correlation
- Protocol reconstruction data
- Unified analysis reports

**Use Case:** Seeing how different binaries interact and share protocols.

---

### `analysis_results_real/` - Real-Time Analysis
Real-time analysis outputs from interactive sessions.

**Contains:**
- Live decompilation results
- Interactive exploration findings
- Ad-hoc analysis notes

**Use Case:** Understanding the analysis process and methodology.

---

### `real_mcp_analysis/` - MCP-Assisted Analysis
Detailed MCP-assisted analysis with AI insights.

**Contains:**
- AI-assisted function interpretation
- Protocol pattern recognition
- Automated documentation generation

**Use Case:** Leveraging AI for understanding complex decompiled code.

---

### `mcp_analysis_results/` - MCP Analysis Data
Raw MCP analysis data and results.

**Contains:**
- JSON data from MCP tools
- Function metadata
- Analysis statistics

**Use Case:** Raw data for tooling and automation.

---

## How to Use This Archive

### For Verification
If you want to verify a specific protocol detail:
1. Check the master guide for the claim
2. Find the relevant function in `analysis/`
3. Cross-reference with decompiled code

### For Research
If you're researching the protocol:
1. Start with `comprehensive_analysis_results/`
2. Drill down into specific functions in `analysis/`
3. Check raw data in `findings/` for validation

### For Reproduction
If you want to reproduce the analysis:
1. Review methodology in `automated_mcp_results/`
2. Check scripts in `../scripts/`
3. Use Ghidra projects in `../ghidra_projects/`

---

## Analysis Statistics

**Total Files:** 90+ (markdown + JSON)  
**Analysis Depth:** 4,674 functions across 8 binaries  
**Documentation:** ~172 KB of detailed analysis  
**Consolidation:** All critical data → 1 master document  

---

## Archive Organization

```
archive/
├── analysis/                         (45+ function analyses)
│   ├── function_SetPeriodic_*.md
│   ├── function_SetConstant_*.md
│   ├── function_SetEnvelope_*.md
│   └── ...
│
├── findings/                         (Raw outputs)
│   ├── *.json                        (JSON summaries)
│   └── *.md                          (Analysis reports)
│
├── automated_mcp_results/            (Automated analysis)
│   └── (MCP decompilation outputs)
│
├── comprehensive_analysis_results/   (Multi-binary)
│   └── (Cross-binary analysis)
│
├── analysis_results_real/            (Real-time)
│   └── (Interactive session outputs)
│
├── real_mcp_analysis/                (MCP-assisted)
│   └── (AI-assisted analysis)
│
└── mcp_analysis_results/             (MCP data)
    └── (Raw MCP JSON data)
```

---

## When to Use the Archive

✅ **Use archive when:**
- Verifying protocol details
- Understanding analysis methodology
- Reproducing research
- Deep-diving into specific functions
- Contributing to documentation

❌ **Don't use archive when:**
- Implementing the driver (use master guide)
- Building and testing (use master guide)
- Learning the protocol (use master guide)
- Quick reference (use master guide)

---

## Maintenance

**Status:** Frozen  
**Updates:** No longer maintained (data consolidated)  
**Version:** As of 2025-01-14  

All future updates go to `MASTER_IMPLEMENTATION_GUIDE.md`.

---

**TL;DR:** This is reference material only. Use the master guide for implementation.
