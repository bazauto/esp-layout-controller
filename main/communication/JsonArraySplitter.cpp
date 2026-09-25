#include "JsonArraySplitter.h"

JsonArraySplitter::JsonArraySplitter(size_t maxElementBytes)
    : m_maxElementBytes(maxElementBytes)
    , m_state(State::BEFORE_ARRAY)
    , m_elementBytes(0)
    , m_depth(0)
    , m_inString(false)
    , m_escaped(false)
    , m_skipping(false)
    , m_oversized(0)
{
}

bool JsonArraySplitter::isWhitespace(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

void JsonArraySplitter::startElement(char opener)
{
    m_element.clear();
    m_element.push_back(opener);
    m_elementBytes = 1;
    m_depth = 1;
    m_inString = false;
    m_escaped = false;
    m_skipping = false;
    m_state = State::IN_ELEMENT;
}

bool JsonArraySplitter::consumeElementChar(char c, const ElementCallback& onElement)
{
    ++m_elementBytes;
    if (!m_skipping) {
        if (m_elementBytes > m_maxElementBytes) {
            // Scanned to its end, but not kept: one oversized record costs a
            // loco, not the roster.
            m_skipping = true;
            std::string().swap(m_element);
        } else {
            m_element.push_back(c);
        }
    }

    if (m_inString) {
        if (m_escaped) {
            m_escaped = false;
        } else if (c == '\\') {
            m_escaped = true;
        } else if (c == '"') {
            m_inString = false;
        }
        return true;
    }

    switch (c) {
        case '"':
            m_inString = true;
            break;
        case '{':
        case '[':
            ++m_depth;
            break;
        case '}':
        case ']':
            if (--m_depth == 0) {
                m_state = State::AFTER_ELEMENT;
                if (m_skipping) {
                    ++m_oversized;
                } else if (onElement && !onElement(m_element)) {
                    return false;
                }
                std::string().swap(m_element);
            }
            break;
        default:
            break;
    }
    return true;
}

bool JsonArraySplitter::feed(const char* data, size_t length, const ElementCallback& onElement)
{
    if (!data) {
        return m_state != State::FAILED;
    }

    for (size_t i = 0; i < length && m_state != State::FAILED; ++i) {
        const char c = data[i];
        switch (m_state) {
            case State::BEFORE_ARRAY:
                if (c == '[') {
                    m_state = State::BEFORE_FIRST_ELEMENT;
                } else if (!isWhitespace(c)) {
                    m_state = State::FAILED;
                }
                break;

            case State::BEFORE_FIRST_ELEMENT:
            case State::BEFORE_ELEMENT:
                if (c == '{' || c == '[') {
                    startElement(c);
                } else if (c == ']' && m_state == State::BEFORE_FIRST_ELEMENT) {
                    m_state = State::DONE;
                } else if (!isWhitespace(c)) {
                    m_state = State::FAILED;
                }
                break;

            case State::IN_ELEMENT:
                if (!consumeElementChar(c, onElement)) {
                    m_state = State::FAILED;
                }
                break;

            case State::AFTER_ELEMENT:
                if (c == ',') {
                    m_state = State::BEFORE_ELEMENT;
                } else if (c == ']') {
                    m_state = State::DONE;
                } else if (!isWhitespace(c)) {
                    m_state = State::FAILED;
                }
                break;

            case State::DONE:
                if (!isWhitespace(c)) {
                    m_state = State::FAILED;
                }
                break;

            case State::FAILED:
                break;
        }
    }
    return m_state != State::FAILED;
}
