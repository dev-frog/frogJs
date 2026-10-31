#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <termios.h>
#include <vector>

namespace frogjs {

// A minimal readline replacement for the REPL. It is fed one byte at a time
// from the event loop (so timers keep running while the user types) and
// supports cursor movement, Emacs-style editing keys, history and tab
// completion. Long lines scroll horizontally on a single terminal row.
class LineEditor {
public:
    enum class Event {
        kNone,       // keep reading
        kLine,       // Enter: the line is complete
        kEof,        // Ctrl+D on an empty line
        kInterrupt,  // Ctrl+C
    };

    // Returns the candidates that can replace the word ending at the cursor,
    // and sets *word_start to where that word begins in line
    using Completer = std::function<std::vector<std::string>(const std::string& line, size_t* word_start)>;

    explicit LineEditor(int fd);
    ~LineEditor();

    void SetCompleter(Completer completer) { completer_ = std::move(completer); }

    // Put the terminal in raw mode and show prompt with an empty line
    void Start(const std::string& prompt);
    // Restore the terminal's original mode (for running code, or on exit)
    void Stop();
    bool active() const { return active_; }

    // Process one input byte; on kLine and kInterrupt *line receives the text
    Event Feed(unsigned char byte, std::string* line);

    // Erase the prompt line / draw it again, around output from async code
    void Hide();
    void Refresh();

    void AddHistory(const std::string& line);
    void LoadHistory(const std::string& path);
    void SaveHistory(const std::string& path) const;

private:
    Event Insert(unsigned char byte);
    void MoveLeft();
    void MoveRight();
    void DeleteBackward();
    void DeleteForward();
    void HistoryStep(int direction);
    void Complete();
    size_t Columns() const;
    void Write(const std::string& text) const;

    int fd_;
    bool active_ = false;
    bool raw_saved_ = false;
    struct termios original_;
    std::string prompt_;
    std::string line_;
    size_t cursor_ = 0;  // byte offset into line_
    std::string escape_;  // pending escape sequence
    std::vector<std::string> history_;
    size_t history_index_ = 0;  // == history_.size() while editing a new line
    std::string draft_;         // the new line, kept while browsing history
    Completer completer_;
};

}  // namespace frogjs
