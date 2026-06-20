#!/bin/bash

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Script info
SCRIPT_NAME="FrogJS Release Automation"
VERSION="1.0.0"

echo -e "${BLUE}======================================${NC}"
echo -e "${BLUE}  $SCRIPT_NAME${NC}"
echo -e "${BLUE}======================================${NC}"
echo ""

# Function to display usage
usage() {
    echo "Usage: $0 [options] [version]"
    echo ""
    echo "Options:"
    echo "  -h, --help           Show this help message"
    echo "  -n, --dry-run        Show what would be done without executing"
    echo "  -c, --create-tag     Create and push new tag (default: auto)"
    echo "  -l, --latest         Push to 'latest' branch as well"
    echo ""
    echo "Examples:"
    echo "  $0 v1.0.0           # Create and push v1.0.0 tag"
    echo "  $0 -n v1.0.0        # Dry run for v1.0.0"
    echo "  $0                  # Interactive mode"
    echo ""
    exit 0
}

# Parse arguments
DRY_RUN=false
CREATE_TAG=true
LATEST=false
VERSION=""

while [[ $# -gt 0 ]]; do
    case $1 in
        -h|--help)
            usage
            ;;
        -n|--dry-run)
            DRY_RUN=true
            shift
            ;;
        -c|--create-tag)
            CREATE_TAG=true
            shift
            ;;
        -l|--latest)
            LATEST=true
            shift
            ;;
        -*)
            echo -e "${RED}Error: Unknown option $1${NC}"
            usage
            ;;
        *)
            VERSION=$1
            shift
            ;;
    esac
done

# Function to run commands (with dry-run support)
run_cmd() {
    local cmd="$1"
    if [ "$DRY_RUN" = true ]; then
        echo -e "${YELLOW}[DRY RUN]${NC} $cmd"
    else
        echo -e "${GREEN}Executing:${NC} $cmd"
        eval "$cmd"
    fi
}

# Function to check if git repo is clean
check_git_status() {
    echo -e "${BLUE}Checking git status...${NC}"

    if [ -n "$(git status --porcelain)" ]; then
        echo -e "${RED}Error: You have uncommitted changes!${NC}"
        echo ""
        echo "Please commit or stash your changes first:"
        git status --short
        echo ""
        echo "Tip: Use 'git stash' to save changes temporarily"
        exit 1
    fi

    echo -e "${GREEN}✓ Git repository is clean${NC}"
    echo ""
}

# Function to get current branch
get_current_branch() {
    git rev-parse --abbrev-ref HEAD
}

# Function to validate version format
validate_version() {
    local version=$1
    if [[ ! $version =~ ^v[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
        echo -e "${RED}Error: Invalid version format: $version${NC}"
        echo "Version must be in format: v1.0.0, v2.3.4, etc."
        exit 1
    fi
    echo -e "${GREEN}✓ Version format is valid: $version${NC}"
}

# Function to check if tag already exists
check_tag_exists() {
    local tag=$1
    if git rev-parse "$tag" >/dev/null 2>&1; then
        echo -e "${RED}Error: Tag $tag already exists!${NC}"
        echo "Use a different version or delete the existing tag first:"
        echo "  git tag -d $tag && git push origin :refs/tags/$tag"
        exit 1
    fi
}

# Function to get version interactively
get_version_interactive() {
    echo -e "${BLUE}Enter release version (format: v1.0.0):${NC}"
    read -r VERSION

    if [ -z "$VERSION" ]; then
        echo -e "${RED}Error: Version cannot be empty${NC}"
        exit 1
    fi

    validate_version "$VERSION"
    check_tag_exists "$VERSION"
}

# Function to show release preview
show_preview() {
    echo ""
    echo -e "${BLUE}======================================${NC}"
    echo -e "${BLUE}  Release Preview${NC}"
    echo -e "${BLUE}======================================${NC}"
    echo -e "Version:      ${GREEN}$VERSION${NC}"
    echo -e "Branch:       ${GREEN}$(get_current_branch)${NC}"
    echo -e "Repository:   ${GREEN}$(git remote get-url origin)${NC}"
    echo -e "Commit:       ${GREEN}$(git rev-parse --short HEAD)${NC}"
    echo ""

    if [ "$LATEST" = true ]; then
        echo -e "${YELLOW}Will also update 'latest' branch${NC}"
    fi

    if [ "$DRY_RUN" = true ]; then
        echo -e "${YELLOW}*** DRY RUN MODE - No changes will be made ***${NC}"
    fi

    echo -e "${BLUE}======================================${NC}"
    echo ""
}

# Function to confirm release
confirm_release() {
    echo -e "${YELLOW}Continue with release? (y/N)${NC}"
    read -r response

    if [[ ! $response =~ ^[Yy]$ ]]; then
        echo -e "${RED}Release cancelled${NC}"
        exit 0
    fi
}

# Function to create and push tag
create_release_tag() {
    echo -e "${BLUE}Creating release tag...${NC}"
    run_cmd "git tag -a $VERSION -m 'Release $VERSION'"
    echo -e "${GREEN}✓ Tag created locally: $VERSION${NC}"
    echo ""

    echo -e "${BLUE}Pushing tag to remote...${NC}"
    run_cmd "git push origin $VERSION"
    echo -e "${GREEN}✓ Tag pushed to remote${NC}"
    echo ""
}

# Function to update latest branch
update_latest_branch() {
    echo -e "${BLUE}Updating latest branch...${NC}"
    run_cmd "git push origin $(get_current_branch):latest --force"
    echo -e "${GREEN}✓ Latest branch updated${NC}"
    echo ""
}

# Function to show monitoring info
show_monitoring_info() {
    echo -e "${BLUE}======================================${NC}"
    echo -e "${BLUE}  Monitoring Your Release${NC}"
    echo -e "${BLUE}======================================${NC}"
    echo ""
    echo -e "${GREEN}GitHub Actions is now building your release!${NC}"
    echo ""
    echo "Watch the build progress:"
    echo -e "${YELLOW}  https://github.com/$(git remote get-url origin | sed 's/.*github.com[:/]\(.*\)\.git/\1/')/actions${NC}"
    echo ""
    echo "Your release will be available at:"
    echo -e "${YELLOW}  https://github.com/$(git remote get-url origin | sed 's/.*github.com[:/]\(.*\)\.git/\1/')/releases/tag/$VERSION${NC}"
    echo ""
    echo "Build artifacts (temporary):"
    echo -e "${YELLOW}  https://github.com/$(git remote get-url origin | sed 's/.*github.com[:/]\(.*\)\.git/\1/')/actions${NC}"
    echo ""
    echo -e "${BLUE}======================================${NC}"
    echo ""
}

# Main execution
main() {
    # Check git status
    check_git_status

    # Get version if not provided
    if [ -z "$VERSION" ]; then
        get_version_interactive
    else
        validate_version "$VERSION"
        check_tag_exists "$VERSION"
    fi

    # Show preview
    show_preview

    # Confirm release
    if [ "$DRY_RUN" = false ]; then
        confirm_release
    fi

    # Create and push tag
    if [ "$CREATE_TAG" = true ]; then
        create_release_tag
    fi

    # Update latest branch if requested
    if [ "$LATEST" = true ]; then
        update_latest_branch
    fi

    # Show monitoring info
    if [ "$DRY_RUN" = false ]; then
        show_monitoring_info
    else
        echo -e "${YELLOW}*** Dry run completed - no actual changes made ***${NC}"
        echo ""
    fi

    echo -e "${GREEN}🚀 Release process completed successfully!${NC}"
}

# Run main function
main