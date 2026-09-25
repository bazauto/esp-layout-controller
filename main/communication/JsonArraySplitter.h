#pragma once

#include <cstddef>
#include <functional>
#include <string>

/**
 * @brief Splits a JSON array arriving in chunks into its top-level elements.
 *
 * The orchestrator's roster is an array of loco records of a few hundred bytes
 * each. Buffering the whole response capped the roster at what fitted in the
 * buffer, and a response over the cap was refused whole: above about 35 locos
 * nothing was selectable (F-35). This hands each element over as soon as it
 * closes, so memory is bounded by one element however long the array is.
 *
 * It finds element boundaries and nothing more: brackets and braces outside
 * strings, and the commas between elements. What is inside an element is not
 * checked here -- the caller parses each one, and cJSON refuses anything
 * malformed. Elements must be objects or arrays; an array of bare numbers or
 * strings is reported as malformed.
 *
 * No ESP-IDF dependency, so it can move into a host-side test build as it is.
 */
class JsonArraySplitter {
public:
    /** Receives one complete element. Returning false fails the stream. */
    using ElementCallback = std::function<bool(const std::string& element)>;

    /** @param maxElementBytes Elements longer than this are skipped, not
     *  buffered; see oversizedElements(). */
    explicit JsonArraySplitter(size_t maxElementBytes);

    /**
     * @brief Consumes the next chunk of the stream.
     * @return false once the stream is malformed or the callback has refused an
     *         element. Everything fed after that is ignored.
     */
    bool feed(const char* data, size_t length, const ElementCallback& onElement);

    /** True once the array's closing bracket has been seen. */
    bool complete() const { return m_state == State::DONE; }

    bool failed() const { return m_state == State::FAILED; }

    /** Elements skipped for exceeding maxElementBytes. */
    size_t oversizedElements() const { return m_oversized; }

private:
    enum class State {
        BEFORE_ARRAY,         ///< waiting for '['
        BEFORE_FIRST_ELEMENT, ///< after '[': an element or ']'
        BEFORE_ELEMENT,       ///< after ',': an element and only an element
        IN_ELEMENT,
        AFTER_ELEMENT,        ///< ',' or ']'
        DONE,                 ///< only whitespace may follow
        FAILED,
    };

    static bool isWhitespace(char c);

    void startElement(char opener);
    bool consumeElementChar(char c, const ElementCallback& onElement);

    size_t m_maxElementBytes;
    State m_state;
    std::string m_element;
    size_t m_elementBytes;
    int m_depth;
    bool m_inString;
    bool m_escaped;
    bool m_skipping;
    size_t m_oversized;
};
