#include <clang/AST/ASTConsumer.h>
#include <clang/AST/ASTContext.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendAction.h>
#include <clang/Tooling/Tooling.h>

#include <iostream>

// based on this older example: https://github.com/kevinaboos/LibToolingExample/blob/master/Example.cpp (accompanied by this older blog: https://kevinaboos.wordpress.com/2013/07/23/clang-tutorial-part-ii-libtooling-example/)
// also more info here: https://clang.llvm.org/docs/RAVFrontendAction.html

class MyASTVisitor : public clang::RecursiveASTVisitor< MyASTVisitor >
{
public:
    explicit MyASTVisitor( clang::CompilerInstance & ci )
        :
        astContext_{ ci.getASTContext() }
    {
    }

    bool VisitVarDecl( clang::VarDecl * varDecl )
    {
        if ( interestingFunction_ )
        {
            std::cout << "Visited variable declaration: " << varDecl->getName().data() << " of type: " << varDecl->getType().getAsString() << ", at location: " << varDecl->getLocation().printToString( astContext_.getSourceManager() ) << std::endl;
        }
        return true;
    }

    bool VisitFunctionDecl( clang::FunctionDecl * funcDecl )
    {
        if ( funcDecl->getIdentifier() != nullptr && funcDecl->getName() == "maxArray" )
        {
            interestingFunction_ = true;
        }
        return true;
    }

private:
    clang::ASTContext & astContext_;
    bool                interestingFunction_{ false };

};

class MyASTConsumer : public clang::ASTConsumer
{
public:
    explicit MyASTConsumer( clang::CompilerInstance & ci )
        :
        visitor_{ ci }
    {}

    virtual void HandleTranslationUnit( clang::ASTContext & context ) override
    {
        visitor_.TraverseDecl( context.getTranslationUnitDecl() );
    }
private:
    MyASTVisitor visitor_;
};

class MyFrontendAction : public clang::ASTFrontendAction
{
public:
    virtual std::unique_ptr< clang::ASTConsumer > CreateASTConsumer( clang::CompilerInstance & ci, llvm::StringRef file ) override
    {
        return std::make_unique< MyASTConsumer >( ci );
    }
};

int main()
{
    auto testCode
    {
        R"cpp(
#include <vector>
void maxArray( std::vector< double > & x, double * y )
{
    auto firstMember{ *x.begin() };
    for ( int i = 0; i < 65536; i++ )
    {
        if ( y[ i ] > x[ i ] ) x[ i ] = y[ i ];
    }
}
        )cpp"
    };

    std::vector< std::string > args{{ "-std=c++20", "-isysroot", "/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX12.3.sdk", "-I/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/lib/clang/13.1.6/include/" }};

    auto success{ clang::tooling::runToolOnCodeWithArgs( std::make_unique< MyFrontendAction >(), testCode, args ) };

    return !success;
}
