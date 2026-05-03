# Publishing Guide

Audience: Project Maintainer

Use GitHub as the canonical manual source, FateMMO.com as the polished entry point, and the forums as the support/community layer.

## Recommended Split

GitHub:

- Canonical docs.
- Versioned with the demo source.
- Good for technical users and contributors.
- Good for diffs and pull requests.

FateMMO.com:

- Public landing page.
- Download buttons.
- Screenshots and short videos.
- "Start here" path for non-developers.
- Links to the current GitHub docs.

Forums:

- Support threads.
- Bug reports from users who do not use GitHub.
- Community map/content showcases.
- FAQ follow-up.
- Release feedback.

## GitHub Structure

Recommended path:

```text
Docs/
  Demo/
    README.md
    quick-start.md
    editor-user-guide.md
    asset-pipeline.md
    architecture-manual.md
    networking-protocol.md
    api-reference.md
    troubleshooting.md
    publishing-guide.md
    tutorials/
      first-map.md
      local-client-server.md
```

Add a short link from the root `README.md`:

```md
## Demo Manual

New users should start with `Documents/Demo/README.md`.
```

## Website Page Draft

Use this as the first `fatemmo.com/docs` page:

```md
# FateMMO Engine Demo Docs

Start here if you want to build, run, or experiment with the FateMMO Game Engine demo.

The GitHub manuals are the source of truth for the current demo release. The website gives you the clean entry point, downloads, screenshots, and community links.

Recommended first steps:

1. Download or clone the demo.
2. Read the Quick Start.
3. Open the editor.
4. Follow My First MMORPG Map.
5. Ask questions or share progress on the forums.

Docs:

- Quick Start
- Editor User Guide
- Asset Pipeline
- Architecture Manual
- Networking Protocol
- API Reference
- Troubleshooting

Community:

- Demo Support Forum
- Known Issues
- Show Your Maps
- Feature Requests
```

## Forum Categories

Suggested pinned threads:

- Demo Support
- Known Issues
- Build Help
- Show Your Maps
- Feature Requests
- Tutorial Feedback
- Release Notes

Suggested forum rule:

```text
Manual pages live in GitHub. Forum answers can clarify, but confirmed fixes should be folded back into the docs.
```

## Release Checklist

Before publishing docs for a demo release:

1. Confirm `PROTOCOL_VERSION` from `engine/net/packet.h`.
2. Confirm demo target behavior from `examples/demo_app.cpp`.
3. Confirm demo component registration from `engine/components/register_engine_components.h`.
4. Run the demo build.
5. Open the editor.
6. Load a scene.
7. Test Play, Stop, and Observe.
8. Check all relative links in `Documents/Demo/`.
9. Link `Documents/Demo/README.md` from the root README.
10. Add or update the website docs page.
11. Pin a forum support thread for the release.
