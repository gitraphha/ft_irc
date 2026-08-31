#include "Parser.hpp"

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

/*
 * Tests for Parser::parse.
 *
 * Parser doesn't have dependencies, so this test links against Parser.cpp
 * alone: no Server, no Client, no handler stubs. The whole IRC message
 * grammar is checked offline, before the poll loop exists.
 *
 * Run: ./tests/test_parser.sh
 */

static int  failures = 0;

static void report(bool ok, const std::string &what) {
    std::cout << (ok ? "  ok   " : "  FAIL ") << what << "\n";
    if (!ok)
        failures++;
}

static void expect(const std::string &label, const std::string &line,
                   bool wantOk, const char *wantCmd, const char *expected[]) {
    std::string              cmd;
    std::vector<std::string> params;

    std::cout << label << "\n";
    bool ok = Parser::parse(line, cmd, params);
    report(ok == wantOk,
           ok ? "parse accepted the line" : "parse rejected the line");
    if (!ok || !wantOk)
        return;
    report(cmd == wantCmd, "command is " + std::string(wantCmd));

    size_t wantCount = 0;
    while (expected[wantCount] != NULL)
        ++wantCount;
    std::ostringstream count;
    count << wantCount << " parameter(s)";
    if (params.size() != wantCount) {
        std::ostringstream got;
        got << count.str() << ", got " << params.size();
        report(false, got.str());
        return;
    }
    report(true, count.str());
    for (size_t i = 0; i < wantCount; i++) {
        std::ostringstream detail;
        detail << "params[" << i << "] is \"" << expected[i] << "\"";
        bool same = params[i] == expected[i];
        if (!same)
            detail << ", got \"" << params[i] << "\"";
        report(same, detail.str());
    }
}

int main() {
    std::cout << "Parser tests: \n\n";

    // An empty message is silently ignored per RFC 2812 section 2.3, so parse
    // reports failure and run() returns without sending anything back.
    {
        const char *none[] = {NULL};
        expect("[empty line]", "", false, "", none);
        expect("[only spaces]", "    ", false, "", none);
    }
    {
        const char *none[] = {NULL};
        expect("[command only]", "QUIT", true, "QUIT", none);
    }
    // Command names are case-insensitive, and registerCommands() keys the
    // table on upper case, so parse folds the name up.
    {
        const char *p[] = {"bob", NULL};
        expect("[lowercase command]", "nick bob", true, "NICK", p);
    }
    // Repeated spaces are separators, not empty parameters. Only a trailing
    // parameter may be empty.
    {
        const char *p[] = {"bob", NULL};
        expect("[double space between params]", "NICK  bob", true, "NICK", p);
        expect("[leading spaces]", "   NICK bob", true, "NICK", p);
        expect("[trailing spaces]", "NICK bob   ", true, "NICK", p);
    }
    // The trailing parameter keeps its spaces. Without it, "hello world"
    // would arrive as two parameters and every PRIVMSG would be truncated at
    // the first space.
    {
        const char *p[] = {"#chan", "hello world", NULL};
        expect("[trailing with spaces]", "PRIVMSG #chan :hello world", true,
               "PRIVMSG", p);
    }
    // Only the first ":" after a space opens the trailing parameter. Any
    // further colon is ordinary text, which matters because clients send
    // colons inside messages constantly.
    {
        const char *p[] = {"#c", ":hi", NULL};
        expect("[colon opening the trailing text]", "PRIVMSG #c ::hi", true,
               "PRIVMSG", p);
    }
    {
        const char *p[] = {"#c", "see: this", NULL};
        expect("[colon later in the trailing text]", "PRIVMSG #c :see: this",
               true, "PRIVMSG", p);
    }
    // An empty trailing is a real parameter and must not be dropped. Losing
    // the empty parameter would silently turn one command into the other.
    {
        const char *p[] = {"#c", "", NULL};
        expect("[empty trailing clears the topic]", "TOPIC #c :", true, "TOPIC",
               p);
    }
    {
        const char *p[] = {"#c", NULL};
        expect("[no trailing queries the topic]", "TOPIC #c", true, "TOPIC", p);
    }
    // A prefix is only a prefix at position 0
    {
        const char *p[] = {"bye", NULL};
        expect("[prefix is skipped]", ":bob!u@h QUIT :bye", true, "QUIT", p);
    }
    {
        const char *none[] = {NULL};
        expect("[prefix with no command]", ":bob!u@h", false, "", none);
    }
    // The registration commands, in the exact shape a real client sends them.
    {
        const char *p[] = {"secret", NULL};
        expect("[PASS]", "PASS secret", true, "PASS", p);
    }
    {
        const char *p[] = {"blanca", "0", "*", "Blanca Nunez", NULL};
        expect("[USER with realname]", "USER blanca 0 * :Blanca Nunez", true,
               "USER", p);
    }
    {
        const char *p[] = {"#chan", "key", NULL};
        expect("[JOIN with key]", "JOIN #chan key", true, "JOIN", p);
    }
    {
        const char *p[] = {"#chan", "+o", "bob", NULL};
        expect("[MODE]", "MODE #chan +o bob", true, "MODE", p);
    }

    std::cout << "\n";
    if (failures == 0)
        std::cout << "all checks passed\n";
    else
        std::cout << failures << " check(s) failed\n";
    return (failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
}
