#include "pico_logic.h"

#include <cassert>
#include <iostream>

int main() {
    using pico_lab::escapeHtml;
    using pico_lab::morseFor;
    using pico_lab::additionalMorseGapUnits;
    using pico_lab::parseSurveyRequest;
    using pico_lab::signalPercent;
    using pico_lab::SurveyRequestResult;

    assert(escapeHtml("Cafe & <guest> \"quoted\" '") == "Cafe &amp; &lt;guest&gt; &quot;quoted&quot; &#39;");
    assert(escapeHtml("abcdef", 3) == "abc");
    assert(signalPercent(-120) == 0);
    assert(signalPercent(-100) == 0);
    assert(signalPercent(-75) == 50);
    assert(signalPercent(-50) == 100);
    assert(signalPercent(-10) == 100);
    assert(morseFor('H') == "....");
    assert(morseFor('w') == ".--");
    assert(morseFor(' ') == "");
    assert(morseFor('?') == "");
    assert(additionalMorseGapUnits('W') + 1 == 3);
    assert(additionalMorseGapUnits(' ') + 1 == 7);
    assert(parseSurveyRequest("GET / HTTP/1.1\r\nHost: 192.168.4.1\r\n\r\n") == SurveyRequestResult::Ok);
    assert(parseSurveyRequest("GET / HTTP/1.0\r\nhost: 192.168.4.1:80\r\n\r\n") == SurveyRequestResult::Ok);
    assert(parseSurveyRequest("GET / HTTP/1.1\r\n\r\n") == SurveyRequestResult::HostRejected);
    assert(parseSurveyRequest("GET / HTTP/1.1\r\nHost: 192.168.4.1\r\nHost: 192.168.4.1\r\n\r\n") == SurveyRequestResult::HostRejected);
    assert(parseSurveyRequest("GET / HTTP/1.1\r\nHost: attacker.example\r\n\r\n") == SurveyRequestResult::HostRejected);
    assert(parseSurveyRequest("POST / HTTP/1.1\r\nHost: 192.168.4.1\r\n\r\n") == SurveyRequestResult::MethodNotAllowed);
    assert(parseSurveyRequest("GET /admin HTTP/1.1\r\nHost: 192.168.4.1\r\n\r\n") == SurveyRequestResult::BadRequest);
    assert(parseSurveyRequest("GET http://192.168.4.1/ HTTP/1.1\r\nHost: 192.168.4.1\r\n\r\n") == SurveyRequestResult::BadRequest);
    assert(parseSurveyRequest("GET / HTTP/1.1\nHost: 192.168.4.1\n\n") == SurveyRequestResult::BadRequest);
    assert(parseSurveyRequest("GET / HTTP/1.1\r\nHost: 192.168.4.1\r\nContent-Length: 1\r\n\r\n") == SurveyRequestResult::BadRequest);

    std::cout << "pico logic tests passed\n";
}
