#include "nus/ast/AstPrinter.hpp"
#include "nus/lexer/Lexer.hpp"
#include "nus/lexer/TokenKind.hpp"
#include "nus/parser/Parser.hpp"
#include "nus/sema/SemanticAnalyzer.hpp"
#include "nus/source/SourceManager.hpp"

#include <exception>
#include <iostream>
#include <string_view>

namespace {

void printDiagnostic(const nus::SourceManager& sources, const nus::Diagnostic& diagnostic) {
    const auto location = sources.location(diagnostic.span.file, diagnostic.span.start);
    std::cerr << sources.path(diagnostic.span.file).string()
              << ':' << location.line << ':' << location.column
              << ": error: " << diagnostic.message << '\n';
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) {
        std::cerr << "usage: nusc <file.nus> [--tokens|--check]\n";
        return 2;
    }

    const std::string_view option = argc == 3 ? std::string_view(argv[2]) : std::string_view{};
    const bool tokens_only = option == "--tokens";
    const bool check_only = option == "--check";
    if (argc == 3 && !tokens_only && !check_only) {
        std::cerr << "nusc: unknown option `" << argv[2] << "`\n";
        return 2;
    }

    try {
        nus::SourceManager sources;
        const auto file = sources.loadFile(argv[1]);
        nus::Lexer lexer(sources, file);
        auto tokens = lexer.tokenize();

        if (tokens_only) {
            for (const auto& token : tokens) {
                const auto location = sources.location(token.span.file, token.span.start);
                std::cout << location.line << ':' << location.column << "  "
                          << nus::tokenKindName(token.kind);
                if (token.kind != nus::TokenKind::EndOfFile) {
                    std::cout << "  `" << sources.text(token.span) << '`';
                }
                std::cout << '\n';
            }
            return 0;
        }

        nus::Parser parser(sources, std::move(tokens));
        const auto ast = parser.parseSourceFile();

        for (const auto& diagnostic : parser.diagnostics()) {
            printDiagnostic(sources, diagnostic);
        }
        if (parser.hasErrors()) return 1;

        nus::sema::SemanticAnalyzer analyzer;
        analyzer.analyze(ast);
        for (const auto& diagnostic : analyzer.diagnostics()) {
            printDiagnostic(sources, diagnostic);
        }
        if (analyzer.hasErrors()) return 1;

        if (check_only) return 0;

        nus::ast::AstPrinter printer;
        std::cout << printer.print(ast);
    } catch (const std::exception& error) {
        std::cerr << "nusc: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
