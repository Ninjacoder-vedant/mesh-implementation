# Agent Guidelines

## General Principles
- Be concise, precise, direct, information-dense, and technical.
- Keep language simple, clear, minimal, and easy to understand, ensuring ideas build up logically with zero redundancy.
- Do not use emojis, em-dashes, fluff, filler, or marketing language.
- Avoid fancy bullet formatting such as bold prefixes. Use simple, plain bullet points.
- Terminology: Use inclusive, modern language at all times.
- Focus on thoroughly understanding and applying underlying core concepts rather than relying on surface jargon.
- Stick strictly to concepts and materials explicitly provided, without introducing outside theories or uncovered material.
- Aim for the shortest possible output that completely satisfies all requirements and achieves the highest evaluation standard.
- Do not cite sources, drop academic names, or include reference lists unless explicitly requested. The only exception is naming the Mesh paper and its authors on the title slide.
- Keep this file lean. Only add new guidelines or project instructions with explicit user permission.
- Keep .gitignore minimal. Only add entries when required and do not bloat it.
- Use conventional commits for all git commit messages.

## Project Scope
- Typst presentation for "Mesh: Compacting Memory Management for C/C++ Applications" (Powers, Tench, Berger, McGregor, PLDI 2019).
- Course project: implement the paper end to end and reproduce its results.
- The talk must fit in at most 14 minutes, including any demo. Plan about 12 to 14 content slides at roughly one minute each; move extra detail to backup slides after the end.
- Suggested time split: problem and motivation 2 min, key idea (meshing, randomization, SplitMesher) 4 min, our implementation 2 min, reproduced results 4 min, issues found 1.5 min, conclusion 0.5 min.
- Every number on a slide must come from our own results, not from memory.

## Slide Design
- Font size must never go below 24pt on any slide, including tables, captions, labels, and diagram text.
- Avoid text-heavy slides. Prefer figures, diagrams, and short phrases over sentences.
- Present at most 3 key points per slide.

## Source Material (paths relative to this folder)
- Paper PDF: ../../reference/Mesh/mesh-pldi19-powers.pdf
- Original code: ../../reference/Mesh/
- Our implementation: ../../mymesh/ (feature map in ../../mymesh/README.md)
- Experiment scripts and raw results: ../../experiments/ and ../../experiments/results/ (figures are the .png files there)
- Full write-up with all numbers and findings: ../../REPORT.md
- Project plan and core feature list: ../../plan.md
- Diagrams and notes on the design: ../../notes/paper.md
- Skills to use Typst are located in .agents/skills/.

## Temporary Testing Workflow
- Use `.tmp/` inside the project root for local test builds, snippet evaluation, and rendering PNG page previews.
- Never write temporary scratch files outside the project directory.
- Clean up all temporary artifacts in `.tmp/` immediately after inspection and testing.
