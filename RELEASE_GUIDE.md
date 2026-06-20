# FrogJS Release Automation

## Quick Start

```bash
# Make the script executable (already done)
chmod +x release.sh

# Run in interactive mode
./release.sh

# Or specify version directly
./release.sh v1.0.0

# Dry run (test without making changes)
./release.sh -n v1.0.0
```

## Features

- ✅ Validates git status (no uncommitted changes)
- ✅ Validates version format (v1.0.0)
- ✅ Checks for duplicate tags
- ✅ Creates annotated git tags
- ✅ Pushes to GitHub automatically
- ✅ Triggers GitHub Actions build
- ✅ Creates GitHub Releases
- ✅ Dry-run mode for testing

## Usage Options

```bash
-h, --help           Show help message
-n, --dry-run        Show what would be done without executing
-l, --latest         Push to 'latest' branch as well
```

## Examples

### Interactive Mode
```bash
./release.sh
# Prompts for version and confirmation
```

### Direct Version
```bash
./release.sh v1.0.0
# Creates and pushes v1.0.0 tag directly
```

### Dry Run
```bash
./release.sh -n v1.0.0
# Shows what would happen without making changes
```

### With Latest Branch
```bash
./release.sh -l v1.0.0
# Also updates 'latest' branch
```

## Release Process

1. **Pre-check**: Script checks for uncommitted changes
2. **Version validation**: Ensures version format is correct
3. **Duplicate check**: Verifies tag doesn't exist
4. **Preview**: Shows release details
5. **Confirmation**: Asks for confirmation (unless dry-run)
6. **Tag creation**: Creates annotated git tag
7. **Push**: Pushes tag to GitHub
8. **GitHub Actions**: Automatically builds and creates release

## What Happens After Release

When you push a tag:

1. **GitHub Actions** starts automatically
2. **Build process**:
   - Installs V8 and libuv on macOS
   - Compiles FrogJS
   - Creates release archive
3. **GitHub Release** created automatically:
   - Professional release notes
   - Download links
   - Installation instructions
4. **Artifacts** available for download

## Monitoring Your Release

After running the script, you'll get links to:
- **GitHub Actions**: Watch the build progress
- **GitHub Release**: Download the final binaries
- **Build Artifacts**: Temporary build artifacts

## Versioning

Follow semantic versioning:
- **v1.0.0** - Major release (breaking changes)
- **v1.1.0** - Minor release (new features)
- **v1.1.1** - Patch release (bug fixes)

## Troubleshooting

### "You have uncommitted changes"
```bash
# Commit your changes
git add .
git commit -m "Your commit message"

# Or stash temporarily
git stash
./release.sh v1.0.0
git stash pop
```

### "Tag already exists"
```bash
# Delete existing tag
git tag -d v1.0.0
git push origin :refs/tags/v1.0.0

# Then create new tag
./release.sh v1.0.0
```

### "Invalid version format"
```bash
# Use correct format: v1.0.0
./release.sh v1.0.0      # ✓ Correct
./release.sh 1.0.0       # ✗ Missing 'v'
./release.sh v1.0        # ✗ Missing patch version
```

## First Release

For your first release:

```bash
# 1. Commit all changes
git add .
git commit -m "Prepare for v1.0.0 release"

# 2. Run release script
./release.sh v1.0.0

# 3. Monitor the build
# Links will be provided in the output
```

## Next Steps

After successful release:
- Monitor GitHub Actions build
- Test the released binary
- Update documentation if needed
- Announce release to users

---

**Pro tip**: Use `./release.sh -n v1.0.0` first to test everything works!