#include "nus/lexer/Lexer.hpp"
#include "nus/lexer/TokenKind.hpp"
#include "nus/source/SourceManager.hpp"

#include <exception>
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: uxc <file.nus>\n";
        return 2;
    }

    try {
        nus::SourceManager sources;
        const auto file = sources.loadFile(argv[1]);
        nus::Lexer lexer(sources, file);

        for (const auto& token : lexer.tokenize()) {
            const auto location = sources.location(token.span.file, token.span.start);
            std::cout << location.line << ':' << location.column << "  "
                      << nus::tokenKindName(token.kind);

            if (token.kind != nus::TokenKind::EndOfFile) {
                std::cout << "  `" << sources.text(token.span) << '`';
            }
            std::cout << '\n';
        }
    } catch (const std::exception& error) {
        std::cerr << "uxc: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
