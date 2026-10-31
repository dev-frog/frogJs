#include "frogjs/line_editor.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace frogjs {

namespace {

const size_t kMaxHistory = 1000;

bool IsContinuationByte(unsigned char byte) {
    return (byte & 0xC0) == 0x80;
}

// Terminal columns used by a UTF-8 string (one per code point)
size_t DisplayWidth(const std::string& text, size_t begin, size_t end) {
    size_t width = 0;
    for (size_t i = begin; i < end && i < text.size(); i++) {
        if (!IsContinuationByte(static_cast<unsigned char>(text[i]))) width++;
    }
    return width;
}

bool IsWordChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$' ||
           static_cast<unsigned char>(c) >= 0x80;
}

std::string CommonPrefix(const std::vector<std::string>& items) {
    std::string prefix = items[0];
    for (const std::string& item : items) {
        size_t n = 0;
        while (n < prefix.size() && n < item.size() && prefix[n] == item[n]) n++;
        prefix.resize(n);
    }
    return prefix;
}

}  // namespace

LineEditor::LineEditor(int fd) : fd_(fd) {}

LineEditor::~LineEditor() {
    Stop();
}

void LineEditor::Start(const std::string& prompt) {
    prompt_ = prompt;
    line_.clear();
    cursor_ = 0;
    escape_.clear();
    history_index_ = history_.size();
    draft_.clear();

    if (!raw_saved_) {
        if (tcgetattr(fd_, &original_) != 0) return;
        raw_saved_ = true;
    }
    // Raw input (no echo, no line buffering, Ctrl+C arrives as a byte), but
    // keep output processing so "\n" from console.log still returns the carriage
    struct termios raw = original_;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_cflag |= CS8;
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(fd_, TCSANOW, &raw);
    active_ = true;
    Refresh();
}

void LineEditor::Stop() {
    if (!active_) return;
    tcsetattr(fd_, TCSANOW, &original_);
    active_ = false;
}

LineEditor::Event LineEditor::Feed(unsigned char byte, std::string* line) {
    if (!escape_.empty()) {
        escape_ += static_cast<char>(byte);
        if (escape_.size() == 2) {
            if (byte == '[' || byte == 'O') return Event::kNone;
            // Alt+B / Alt+F: move by word
            if (byte == 'b') {
                while (cursor_ > 0 && !IsWordChar(line_[cursor_ - 1])) cursor_--;
                while (cursor_ > 0 && IsWordChar(line_[cursor_ - 1])) cursor_--;
            } else if (byte == 'f') {
                while (cursor_ < line_.size() && !IsWordChar(line_[cursor_])) cursor_++;
                while (cursor_ < line_.size() && IsWordChar(line_[cursor_])) cursor_++;
            }
            escape_.clear();
            Refresh();
            return Event::kNone;
        }
        if (byte < 0x40 || byte > 0x7E) return Event::kNone;  // parameter bytes

        std::string sequence = escape_.substr(1);
        escape_.clear();
        if (sequence == "[A" || sequence == "OA") HistoryStep(-1);
        else if (sequence == "[B" || sequence == "OB") HistoryStep(1);
        else if (sequence == "[C" || sequence == "OC") MoveRight();
        else if (sequence == "[D" || sequence == "OD") MoveLeft();
        else if (sequence == "[H" || sequence == "OH" || sequence == "[1~" || sequence == "[7~") cursor_ = 0;
        else if (sequence == "[F" || sequence == "OF" || sequence == "[4~" || sequence == "[8~") cursor_ = line_.size();
        else if (sequence == "[3~") DeleteForward();
        Refresh();
        return Event::kNone;
    }

    switch (byte) {
        case '\r':
        case '\n':
            *line = line_;
            cursor_ = line_.size();
            Refresh();
            Write("\n");
            return Event::kLine;
        case 3:  // Ctrl+C
            *line = line_;
            Write("\n");
            return Event::kInterrupt;
        case 4:  // Ctrl+D
            if (line_.empty()) {
                Write("\n");
                return Event::kEof;
            }
            DeleteForward();
            break;
        case 27:
            escape_ = "\x1b";
            return Event::kNone;
        case 1: cursor_ = 0; break;                 // Ctrl+A
        case 5: cursor_ = line_.size(); break;      // Ctrl+E
        case 2: MoveLeft(); break;                  // Ctrl+B
        case 6: MoveRight(); break;                 // Ctrl+F
        case 8:
        case 127: DeleteBackward(); break;          // Backspace
        case 9: Complete(); return Event::kNone;    // Tab
        case 11: line_.erase(cursor_); break;       // Ctrl+K
        case 12: Write("\x1b[H\x1b[2J"); break;     // Ctrl+L
        case 14: HistoryStep(1); return Event::kNone;   // Ctrl+N
        case 16: HistoryStep(-1); return Event::kNone;  // Ctrl+P
        case 21:                                    // Ctrl+U
            line_.erase(0, cursor_);
            cursor_ = 0;
            break;
        case 23: {                                  // Ctrl+W
            size_t start = cursor_;
            while (start > 0 && line_[start - 1] == ' ') start--;
            while (start > 0 && line_[start - 1] != ' ') start--;
            line_.erase(start, cursor_ - start);
            cursor_ = start;
            break;
        }
        default:
            return Insert(byte);
    }
    Refresh();
    return Event::kNone;
}

LineEditor::Event LineEditor::Insert(unsigned char byte) {
    if (byte < 32) return Event::kNone;  // other control characters
    line_.insert(cursor_, 1, static_cast<char>(byte));
    cursor_++;
    Refresh();
    return Event::kNone;
}

void LineEditor::MoveLeft() {
    if (cursor_ == 0) return;
    do {
        cursor_--;
    } while (cursor_ > 0 && IsContinuationByte(static_cast<unsigned char>(line_[cursor_])));
}

void LineEditor::MoveRight() {
    if (cursor_ >= line_.size()) return;
    do {
        cursor_++;
    } while (cursor_ < line_.size() && IsContinuationByte(static_cast<unsigned char>(line_[cursor_])));
}

void LineEditor::DeleteBackward() {
    if (cursor_ == 0) return;
    size_t end = cursor_;
    MoveLeft();
    line_.erase(cursor_, end - cursor_);
}

void LineEditor::DeleteForward() {
    if (cursor_ >= line_.size()) return;
    size_t start = cursor_;
    MoveRight();
    line_.erase(start, cursor_ - start);
    cursor_ = start;
}

void LineEditor::HistoryStep(int direction) {
    if (history_.empty()) return;
    if (direction < 0 && history_index_ == 0) return;
    if (direction > 0 && history_index_ >= history_.size()) return;
    if (history_index_ == history_.size()) draft_ = line_;
    history_index_ += direction;
    line_ = history_index_ == history_.size() ? draft_ : history_[history_index_];
    cursor_ = line_.size();
    Refresh();
}

void LineEditor::Complete() {
    if (!completer_) return;
    size_t word_start = cursor_;
    std::vector<std::string> matches = completer_(line_.substr(0, cursor_), &word_start);
    if (matches.empty() || word_start > cursor_) {
        Write("\x07");
        return;
    }

    // Extend the word as far as all candidates agree
    std::string common = CommonPrefix(matches);
    if (common.size() > cursor_ - word_start) {
        line_.replace(word_start, cursor_ - word_start, common);
        cursor_ = word_start + common.size();
        Refresh();
        return;
    }
    if (matches.size() == 1) return;

    // Otherwise list the candidates in columns under the prompt
    size_t width = 0;
    for (const std::string& match : matches) width = std::max(width, DisplayWidth(match, 0, match.size()));
    width += 2;
    size_t per_row = std::max<size_t>(1, Columns() / width);
    std::string listing = "\r\n";
    for (size_t i = 0; i < matches.size(); i++) {
        listing += matches[i];
        bool row_end = (i + 1) % per_row == 0 || i + 1 == matches.size();
        if (row_end) {
            listing += "\r\n";
        } else {
            listing.append(width - DisplayWidth(matches[i], 0, matches[i].size()), ' ');
        }
    }
    Write(listing);
    Refresh();
}

void LineEditor::Hide() {
    if (active_) Write("\r\x1b[0K");
}

// Redraw prompt and line. If they don't fit, scroll so the cursor stays visible.
void LineEditor::Refresh() {
    if (!active_) return;
    size_t columns = Columns();
    size_t prompt_width = DisplayWidth(prompt_, 0, prompt_.size());
    size_t start = 0;
    while (start < cursor_ && prompt_width + DisplayWidth(line_, start, cursor_) >= columns) {
        do {
            start++;
        } while (start < cursor_ && IsContinuationByte(static_cast<unsigned char>(line_[start])));
    }
    size_t end = line_.size();
    while (end > cursor_ && prompt_width + DisplayWidth(line_, start, end) > columns) {
        do {
            end--;
        } while (end > cursor_ && IsContinuationByte(static_cast<unsigned char>(line_[end])));
    }

    std::string out = "\r" + prompt_ + line_.substr(start, end - start) + "\x1b[0K\r";
    size_t column = prompt_width + DisplayWidth(line_, start, cursor_);
    if (column > 0) out += "\x1b[" + std::to_string(column) + "C";
    Write(out);
}

void LineEditor::AddHistory(const std::string& line) {
    if (line.find_first_not_of(" \t") == std::string::npos) return;
    if (!history_.empty() && history_.back() == line) return;
    history_.push_back(line);
    if (history_.size() > kMaxHistory) history_.erase(history_.begin());
    history_index_ = history_.size();
}

void LineEditor::LoadHistory(const std::string& path) {
    std::ifstream file(path);
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty()) history_.push_back(line);
    }
    if (history_.size() > kMaxHistory) {
        history_.erase(history_.begin(), history_.end() - kMaxHistory);
    }
    history_index_ = history_.size();
}

void LineEditor::SaveHistory(const std::string& path) const {
    std::ofstream file(path, std::ios::trunc);
    if (!file.is_open()) return;
    for (const std::string& line : history_) file << line << "\n";
    file.close();
    chmod(path.c_str(), 0600);
}

size_t LineEditor::Columns() const {
    struct winsize size;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_col > 0) return size.ws_col;
    return 80;
}

void LineEditor::Write(const std::string& text) const {
    fflush(stdout);
    size_t written = 0;
    while (written < text.size()) {
        ssize_t n = write(STDOUT_FILENO, text.data() + written, text.size() - written);
        if (n <= 0) break;
        written += static_cast<size_t>(n);
    }
}

}  // namespace frogjs
