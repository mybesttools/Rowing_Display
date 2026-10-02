# Higgsfield background: blog images

| File | What it shows |
|------|---------------|
| `before-procedural-scene.png` | Old, fully procedural scene (catch / mid / finish / idle). Shaded bands are where the metric tiles sit. |
| `candidate-a-nano-banana-pro.jpg` | Candidate A, Nano Banana Pro (picked). |
| `candidate-b-seedream-5-pro.jpg` | Candidate B, Seedream 5.0 Pro. |
| `candidate-c-flux-2.jpg` | Candidate C, FLUX.2. Its sun reflection is in the wrong place, so it was dropped. |
| `candidates-on-panel.png` | All three cropped to 400×240 RGB565, roughly as the panel shows them. |
| `after-photo-background.png` | Final: the Higgsfield photo with the procedural rower drawn on top. |

All three candidates came from the same 16:9 prompt. `scripts/gen_scene_background.py` bakes the chosen one into the firmware.

## Prompt

> Wide landscape photograph, view from low behind and above a calm lake at dusk, looking straight down the lake toward the horizon. Horizon line high in the frame at about one quarter from the top. Low warm sun just above the horizon on the left third, soft orange-pink glow, gradient sky from deep blue at the top to peach at the horizon. Silhouetted rolling hills and a dark tree line along the far shore. Glassy water filling the lower three quarters of the frame, with a broken golden sun path of glints below the sun and gentle small ripples. No boats, no people, no text, empty water in the center foreground. Painterly but clean, high contrast, simple shapes that read well at low resolution.
