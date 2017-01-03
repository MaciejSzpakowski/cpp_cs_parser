// Maciej Szakowski
// it takes C++ source files and produces monolithic header and source file 
// IR is the intermediate representation of all cpp modules that can be dumped to one monolithic source file

// syntax restrictions
// * everything must be in a namespace except main and includes (which must be in global scope)
// * block comment open token must be the first token of the line and close token must be last
// * includes, field/namespace/struct/class declaration and function prototypes must be one liners, one exception: 
//   function prototypes can span more lines but breaks must occur after coma that separates params
//   that includes initializer lists (coma separates fields inits)
// * {} for functions, structs, enum classes and namespaces must be on separate lines
// * function body must start on the new line (i.e. '{' that starts a function must be first non-space char of the line right after prototype)
// * line comments will be trimmed, line comment is the first occurence of // in a line, dont put any // inside of a string
// * structs can have only fields and methods (no other structs)
// * no old school enums, enum class only
// * dont put block comments in funny places e.g. between prototype and '{'
// * using statements only in namespaces
// * pure virtual methods must be one line

#include <fstream>
#include <vector>
#include <string>
#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <functional>
#include <algorithm>
#include <sstream>
#include <regex>
#include <type_traits>

using std::vector;
using std::string;

namespace util
{
    bool startsWith(const string& s, const string& start)
    {
        return s.find(start) == 0;
    }

    template <typename T>
    T first_or_default(const vector<T>& v, std::function<bool(T)> pred)
    {
        for (int i = 0; i < v.size(); i++)
            if (pred(v.at(i)))
                return v.at(i);

        if (std::is_pointer<T>::value)
            return nullptr;
        else
            return T();
    }

    void syntaxError(int line, const string& filename, const char* msg)
    {
        std::stringstream str;
        str << "Syntax error" << filename << ":" << line;
        if (msg != 0)
            str << " " << msg;

        throw std::runtime_error(str.str().c_str());
    }

    string trim(const string& str)
    {
        if (str.length() == 0)
            return str;

        string newstr = str;

        while (newstr.back() == ' ')
            newstr.pop_back();

        int start;
        for (start = 0; start < (int)newstr.length() && str.at(start) == ' '; start++)
        {
        }

        return newstr.substr(start);
    }

    string removeLineComment(const string& s)
    {
        std::smatch result;
        std::regex comment("\\s*//");
        std::regex_search(s, result, comment);

        if (result.size() == 0)
            return s;

        return s.substr(0, result.position(0));
    }

    bool endsWith(const string& s, const string& end)
    {
        if (s.length() < end.length())
            return false;

        return s.rfind(end) == (s.length() - end.length());
    }
}

namespace monolith
{
    enum class AccessSpecifier
    {
        Private, Public, NoSpecifier, Protected
    };

    class IDump
    {
    public:
        virtual void Dump(std::ostream& header, std::ostream& source) = 0;
    };

    class BaseFunc
    {
    protected:
        std::string body;
        std::string _namespace;
        std::string prototype;
    public:
        BaseFunc(const string& ns)
            : _namespace(ns)
        {
        }

        void AddProto(const string& s)
        {
            prototype += s;
        }

        void AddBody(const string& s)
        {
            body += s;
        }

        // where name starts
        // this method assumes that name is right before first '('
        int GetNameIndex()
        {
            string proto(prototype);
            // find first '('
            int index = proto.find('(');
            // find id
            std::smatch m;
            std::regex r("[~_a-zA-Z0-9]+\\s*\\(");
            std::regex_search(proto, m, r);

            if (m.size() == 0)
                throw std::runtime_error("nameIndex() no id found before '('");

            return m.position(0);
        }
    };

    class Method : public IDump, public BaseFunc
    {
    private:
        std::string initializerList;
    public:
        Method(const string& ns) 
            :BaseFunc(ns)
        {
        }

        // separate prototype from initializer list
        void SplitProto()
        {
            int openparencounter = 0;

            for(int i=0;i<(int)prototype.size();i++)
            {
                if (prototype.at(i) == '(')
                    openparencounter++;
                else if (prototype.at(i) == ')')
                    openparencounter--;

                if (prototype.at(i) == ':' && openparencounter == 0)
                {
                    initializerList = prototype.substr(i);
                    prototype = prototype.substr(0, i);
                    break;
                }
            }
        }

        void Dump(std::ostream& header, std::ostream& source) override
        {
            // prototype in header
            header << prototype << ';' << std::endl;
            header << std::endl;

            // implementation in source
            // insert namespace
            string implProto(prototype);
            implProto.insert(GetNameIndex(), _namespace + "::");

            source << implProto << initializerList << std::endl;
            source << body << std::endl;
            source << std::endl;
        }
    };

    class Function : public BaseFunc, public IDump
    {
    public:
        Function(const string& ns)
            :BaseFunc(ns)
        {
        }

        void Dump(std::ostream& header, std::ostream& source) override
        {
            // main
            if (util::startsWith(prototype, "int main("))
            {
                source << prototype << std::endl;
                source << body << std::endl;
                source << std::endl;
            }
            else
            {
                // prototype in header
                header << "namespace " << _namespace << " {" << std::endl;
                header << "     " << prototype << ";}" << std::endl;
                header << std::endl;

                // implementation in source
                // insert namespace
                string implProto(prototype);
                implProto.insert(GetNameIndex(), _namespace + "::");

                source << implProto << std::endl;
                source << body << std::endl;
                source << std::endl;
            }
        }
    };

    // struct member
    class Field : public IDump
    {
    private:
        std::string prototype;
    public:
        Field(const string& proto)
        {
            prototype = proto;
        }

        void Dump(std::ostream& header, std::ostream& source) override
        {
            header << prototype << std::endl;
        }
    };

    // the difference between variable and nsvariable is that ns variable needs to dump extern in header
    // ns member
    class NsVariable : public IDump
    {
    private:
        string prototype;
        string _namespace;
    public:
        NsVariable(const string& proto, const string& ns):
            prototype(proto), _namespace(ns)
        {
        }

        int GetNameIndex() const
        {
            std::smatch m;
            std::regex r("[_a-z0-9A-Z]+;");
            std::regex_search(prototype, m, r);

            return m.position(0);
        }

        void Dump(std::ostream& header, std::ostream& source) override
        {
            header << "namespace " << _namespace << " {" << std::endl;
            header << "    extern " << prototype << "}" << std::endl;
            header << std::endl;

            string implprototype = prototype;
            implprototype.insert(GetNameIndex(), _namespace + "::");

            source << implprototype << std::endl;
        }
    };

    class EnumClass : public IDump
    {
    private:
        string prototype;
        string name;
        string body;
        string _namespace;
    public:
        EnumClass(string proto, const string& ns):
            prototype(proto), _namespace(ns)
        {
            std::smatch m;
            std::regex r("[_a-zA-Z0-9]+");
            int whereNameStarts = string("enum class ").length();
            string protostartingwithname(prototype.substr(whereNameStarts));
            std::regex_search(protostartingwithname, m, r);

            name = m[0].str();
        }

        void AddBody(const string& str)
        {
            body += str;
        }

        // simple prototype is just enum class + name (no specifier part)
        string GetSimplePrototype() const
        {
            return "enum class " + name;
        }

        void DumpForwardDecl(std::ostream& header)
        {
            header << "namespace " << _namespace << " {" << std::endl;
            header << GetSimplePrototype() << ";}" << std::endl;
            header << std::endl;
        }

        void Dump(std::ostream& header, std::ostream& source) override
        {
            header << "namespace " << _namespace << std::endl;
            header << "{" << std::endl;
            header << prototype << std::endl;
            header << body << std::endl;
            header << "}" << std::endl;
            header << std::endl;
        }
    };

    class Using : public IDump
    {
    private:
        string prototype;
        string _namespace;
    public:
        Using(const string& proto, const string& ns) 
            :prototype(proto), _namespace(ns)
        {
        }

        void Dump(std::ostream& header, std::ostream& source) override
        {
            header << "namespace " << _namespace << std::endl;
            header << "{" << std::endl;
            header << prototype << std::endl;
            header << "}" << std::endl;
            header << std::endl;
        }
    };

    enum class SCType
    {
        Struct, Class
    };

    class StructClass : public IDump
    {
    protected:
        string prototype;
        string name;
        string _namespace;
        vector<IDump*> members; // outside private/public
        vector<IDump*> privateMembers;
        vector<IDump*> publicMembers;
        vector<IDump*> protectedMembers;
    public:
        StructClass(const string& proto, const string& ns):
            prototype(proto), _namespace(ns)
        {
            std::smatch m;
            std::regex r(" [_a-zA-Z0-9]+");
            std::regex_search(prototype, m, r);

            name = m[0].str().substr(1); // substr(1) because it starts with space
        }

        const string& GetName() const
        {
            return name;
        }

        // return struct or class
        SCType GetType() const
        {
            if (util::startsWith(prototype, "struct"))
                return SCType::Struct;
            else
                return SCType::Class;
        }

        // simple prototype is just class/struct + name (no inheritance part)
        string GetSimplePrototype() const
        {
            if (util::startsWith(prototype, "class"))
                return "class " + name;
            else
                return "struct " + name;
        }

        void AddMember(IDump* m, AccessSpecifier acc)
        {
            if (acc == AccessSpecifier::NoSpecifier)
                members.push_back(m);
            else if (acc == AccessSpecifier::Private)
                privateMembers.push_back(m);
            else if (acc == AccessSpecifier::Protected)
                protectedMembers.push_back(m);
            else if (acc == AccessSpecifier::Public)
                publicMembers.push_back(m);
        }

        void DumpForwardDecl(std::ostream& header)
        {
            header << "namespace " << _namespace << " {" << std::endl;
            header << GetSimplePrototype() << ";}" << std::endl;
            header << std::endl;
        }

        void Dump(std::ostream& header, std::ostream& source) override
        {
            header << "namespace " << _namespace << " {" << std::endl;
            header << prototype << std::endl;
            header << '{' << std::endl;
            
            for (IDump* m : members)
                m->Dump(header, source);

            if (privateMembers.size() > 0)
            {
                header << "private:" << std::endl;
            }

            for (IDump* m : privateMembers)
                m->Dump(header, source);

            if (protectedMembers.size() > 0)
            {
                header << "protected:" << std::endl;
            }

            for (IDump* m : protectedMembers)
                m->Dump(header, source);

            if (publicMembers.size() > 0)
            {
                header << "public:" << std::endl;
            }

            for (IDump* m : publicMembers)
                m->Dump(header, source);

            header << "};}" << std::endl << std::endl;
        }
    };

    /*
    class Namespace : public IDump
    {
    private:
        string name;
        vector<Namespace*> namespaces;
        vector<StructClass*> structClasses;
        vector<Function*> functions;
        vector<NsVariable*> variables;
        vector<EnumClass*> enums;
        vector<Using*> usings;
        vector<IDump*> everything;
    public:
        Namespace(const string& proto)
            :name(proto.substr(10))
        {
        }

        void AddNamespace(Namespace* ns)
        {
            namespaces.push_back(ns);
        }

        void AddStructClass(StructClass* sc)
        {
            structClasses.push_back(sc);
        }

        void AddFunction(Function* fun)
        {
            functions.push_back(fun);
        }

        void AddVariable(NsVariable* var)
        {
            variables.push_back(var);
        }

        void AddEnum(EnumClass* enumClass)
        {
            enums.push_back(enumClass);
        }

        void AddUsing(Using* u)
        {
            usings.push_back(u);
        }

        const string& GetName() const
        {
            return name;
        }

        // struct, classes, enum classes
        void DumpForwardDeclarations(std::ostream& header)
        {
            header << headertab;
            header << "namespace " << name << std::endl;
            header << headertab;
            header << '{' << std::endl;

            headertab += "    ";

            for (StructClass* sc : structClasses)
            {
                header << headertab;
                header << sc->GetSimplePrototype() << ";" << std::endl;
            }

            for (EnumClass* e : enums)
            {
                header << headertab;
                header << e->GetSimplePrototype() << ";" << std::endl;
            }

            for (Namespace* ns : namespaces)
                ns->DumpForwardDeclarations(header);

            headertab.pop_back();
            headertab.pop_back();
            headertab.pop_back();
            headertab.pop_back();

            header << headertab;
            header << "}" << std::endl << std::endl;
        }

        void Dump(std::ostream& header, std::ostream& source) override
        {
            header << headertab;
            header << "namespace " << name << std::endl;

            header << headertab;
            header << '{' << std::endl;

            source << sourcetab;
            source << "namespace " << name << std::endl;

            source << sourcetab;
            source << '{' << std::endl;

            headertab += "    ";
            sourcetab += "    ";

            for (IDump* i : usings)
                i->Dump(header, source);

            header << std::endl;

            for (IDump* i : enums)
                i->Dump(header, source);

            for (IDump* i : structClasses)
                i->Dump(header, source);

            for (IDump* i : functions)
                i->Dump(header, source);

            for (IDump* i : namespaces)
                i->Dump(header, source);

            for (IDump* i : variables)
                i->Dump(header, source);

            headertab.pop_back();
            headertab.pop_back();
            headertab.pop_back();
            headertab.pop_back();

            sourcetab.pop_back();
            sourcetab.pop_back();
            sourcetab.pop_back();
            sourcetab.pop_back();

            header << headertab;
            header << "}" << std::endl << std::endl;

            source << sourcetab;
            source << "}" << std::endl << std::endl;
        }
    };
    */


    class Monolith
    {
    private:        
        vector<string> includes;
        vector<StructClass*> structClasses;
        vector<Function*> functions;
        vector<NsVariable*> variables;
        vector<EnumClass*> enums;
        vector<Using*> usings;
        Function* main;

        int lineNum;
        string filename; // currently parsed file, used to error messages
        std::function<bool(string&)> next;  // read next source code line to the string, return true if eof
        string currentNamespace;

        //////////////////////////
        //// PARSER FUNCTIONS ////
        //////////////////////////

        void enterNamespace(const string& str)
        {
            if (currentNamespace.length() > 0)
                currentNamespace += "::";

            currentNamespace += str;
        }

        void exitNamespace()
        {
            while (currentNamespace.length() > 0 && currentNamespace.back() != ':')
                currentNamespace.pop_back();

            if(currentNamespace.length() > 0)
                currentNamespace.pop_back();
        }
        
        Function* ExtractFunction(string& line)
        {
            Function* fun = new Function(currentNamespace);

            // get prototype first
            fun->AddProto(line);
            next(line);
            line = util::removeLineComment(line);

            // prototype goes until line == '{'
            while(line != "{")
            {
                fun->AddProto(line);
                if (next(line))
                    util::syntaxError(lineNum, filename, "unexpected EOF");
                line = util::removeLineComment(line);
            }

            // start counting braces
            // fun body will end when matching '}' encountered
            int openBrace = 1;

            // get body
            fun->AddBody(line);

            do
            {
                if (next(line))
                    util::syntaxError(lineNum, filename, "unexpected EOF");

                fun->AddBody(line);

                openBrace += std::count(line.begin(), line.end(), '{');
                openBrace -= std::count(line.begin(), line.end(), '}');
            } while (openBrace > 0); // keep going until matching closing brace

            return fun;
        }

        Method* ExtractMethod(string& line)
        {
            Method* method = new Method(currentNamespace);

            // get prototype first
            method->AddProto(line);
            next(line);
            line = util::removeLineComment(line);

            // prototype goes until line == '{'
            while (line != "{")
            {
                method->AddProto(line);
                if (next(line))
                    util::syntaxError(lineNum, filename, "unexpected EOF");
                line = util::removeLineComment(line);
            }
            
            method->SplitProto();

            // start counting braces
            // fun body will end when matching '}' encountered
            int openBrace = 1;

            // get body
            method->AddBody(line);

            do
            {
                if (next(line))
                    util::syntaxError(lineNum, filename, "unexpected EOF");

                method->AddBody(line);

                openBrace += std::count(line.begin(), line.end(), '{');
                openBrace -= std::count(line.begin(), line.end(), '}');
            } while (openBrace > 0); // keep going until matching closing brace

            return method;
        }

        EnumClass* ExtractEnumClass(const string& prototype)
        {
            EnumClass* enumClass = new EnumClass(prototype, currentNamespace);

            string line;

            // next line must be '{'
            next(line);
            line = util::removeLineComment(line);

            if (line != "{")
                util::syntaxError(lineNum, filename, "missing '{'");

            enumClass->AddBody(line);

            while (true)
            {
                next(line);

                if (util::startsWith(line, "/*"))
                {
                    while (!util::endsWith(line, "*/"))
                        next(line);
                }

                line = util::removeLineComment(line);
                enumClass->AddBody(line);

                if (line == "};")
                    break;
            }

            return enumClass;
        }

        StructClass* ExtractStructClass(const string& prototype)
        {
            StructClass* structClass = new StructClass(prototype, currentNamespace);
            string line;
            AccessSpecifier accSpecifier = AccessSpecifier::NoSpecifier;

            // next line must be '{'
            next(line);
            line = util::removeLineComment(line);

            if (line != "{")
                util::syntaxError(lineNum, filename, "missing '{'");

            while (true)
            {
                next(line);
                line = util::removeLineComment(line);

                if (line == "private:")
                    accSpecifier = AccessSpecifier::Private;
                else if (line == "public:")
                    accSpecifier = AccessSpecifier::Public;
                else if (line == "protected:")
                    accSpecifier = AccessSpecifier::Protected;
                else if (line == "};")
                    break;
                else if (util::endsWith(line, ";"))
                {
                    Field* field = new Field(line);
                    structClass->AddMember(field, accSpecifier);
                }
                else if (util::endsWith(line, ")") || util::endsWith(line, ","))
                {
                    Method* m = ExtractMethod(line);
                    structClass->AddMember(m, accSpecifier);
                }
                else if (util::startsWith(line, "/*"))
                {
                    while (!util::endsWith(line, "*/"))
                        next(line);
                }       
                else if (line.length() == 0)
                {
                }
                else
                    util::syntaxError(lineNum, filename, "unknown struct member");
            }

            return structClass;
        }

        void ExtractNamespace(string& line)
        {
            enterNamespace(line.substr(10));

            // match '{'
            next(line);

            if (line != "{")
                util::syntaxError(lineNum, filename, "{ expected");

            while (true)
            {
                next(line);
                line = util::removeLineComment(line);

                if (util::startsWith(line, "using"))
                {
                    Using* u = new Using(line, currentNamespace);
                    usings.push_back(u);
                }
                else if (util::endsWith(line, ";"))
                {
                    NsVariable* var = new NsVariable(line, currentNamespace);
                    variables.push_back(var);
                }
                else if (util::endsWith(line, ")") || util::endsWith(line, ","))
                {
                    Function* fun = ExtractFunction(line);
                    functions.push_back(fun);
                }
                else if (util::startsWith(line, "/*"))
                {
                    while (!util::endsWith(line, "*/"))
                        next(line);
                }
                else if (util::startsWith(line, "enum class"))
                {                    
                    EnumClass* e = ExtractEnumClass(line);
                    enums.push_back(e);
                }
                else if (util::startsWith(line, "class") || util::startsWith(line, "struct"))
                {
                    StructClass* s = ExtractStructClass(line);
                    structClasses.push_back(s);
                }
                else if (line.length() == 0)
                {
                }
                else if (util::startsWith(line, "namespace"))
                {
                    ExtractNamespace(line);
                }
                else if (line == "}")
                {
                    break;
                }
                else
                {
                    util::syntaxError(lineNum, filename, "unknown namespace element");
                }
            }

            exitNamespace();
        }

        // parser entry point
        void Program()
        {
            string line;

            while (!next(line))
            {
                line = util::removeLineComment(line);

                if (util::startsWith(line, "#include"))
                    includes.push_back(line);
                
                else if (util::startsWith(line, "/*"))
                {
                    while (!util::endsWith(line, "*/"))
                        next(line);
                }
                else if (line.length() == 0)
                {
                }
                else if (util::startsWith(line, "namespace"))
                {
                    ExtractNamespace(line);
                }
                else if (util::startsWith(line,"int main("))
                {
                    main = ExtractFunction(line);
                }
                else
                {
                    util::syntaxError(lineNum, filename, "unknown program element");
                }
            }
        }

        /////////////////// parser functions end

        void Collect(const string& filename)
        {
            this->filename = filename;
            std::ifstream file(filename);

            if (!file.is_open())
                throw std::runtime_error(("could not open " + filename).c_str());

            next = [this, &file](string& str)
            {
                std::getline(file, str);
                lineNum++;

                str = util::trim(str);

                return file.eof();
            };
            string line;

            Program();

            file.close();
        }

        void DependencyOrder()
        {
        }
    public:
        // ctor is the main driver, it will produce IR of all C++ source files
        Monolith(const vector<string>& filenames):
            lineNum(0)
        {
            for (const string& s : filenames)
                Collect(s);

            DependencyOrder();
        }

        // output IR
        void Dump(std::ostream& header, std::ostream& source, const string& hfile)
        {
            header << "#pragma once" << std::endl;

            for (string& s : includes)
                header << s << std::endl;

            header << '\n';
            source << "#include \"" << hfile << "\"" << std::endl;
            source << std::endl;
            
            DumpForwardDeclaration(header);
            
            // dump main
            main->Dump(header, source);

            Dump2(header, source);
        }

        void DumpForwardDeclaration(std::ostream& header)
        {
            for (StructClass* sc : structClasses)
                sc->DumpForwardDecl(header);

            for (EnumClass* e : enums)
                e->DumpForwardDecl(header);
        }

        void Dump2(std::ostream& header, std::ostream& source)
        {
            for (IDump* i : usings)
                i->Dump(header, source);

            header << std::endl;

            for (IDump* i : enums)
                i->Dump(header, source);

            for (IDump* i : structClasses)
                i->Dump(header, source);

            for (IDump* i : functions)
                i->Dump(header, source);

            for (IDump* i : variables)
                i->Dump(header, source);
        }
    };
}

int main(int argc, char** argv)
{
    vector<string> args;
    for (int i = 1; i < argc; i++)
        args.push_back(argv[i]);

    args.push_back("C:/Users/Szpak/Documents/Visual Studio 2015/Projects/CppHeaderBuilder/CppHeaderBuilder/test1.cpp");

    try
    {
        monolith::Monolith mono(args);
        mono.Dump(std::ofstream("header.h"), 
            std::ofstream("source.cpp"), "header.h");
        //mono.Dump(std::cout, std::cout, "header.h");
    }
    catch (std::runtime_error& e)
    {
        printf("%s\n", e.what());
    }
}
