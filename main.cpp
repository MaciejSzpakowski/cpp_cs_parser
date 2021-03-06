// Maciej Szakowski
// it takes C++ source files and produces monolithic header and source file 
// IR is the intermediate representation of all cpp modules that can be dumped to one monolithic source file

// syntax restrictions
// * everything must be in a namespace except main and includes (which must be in global scope)
// * #define must be one line
// * no structs/classes etc in other struct/classes
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
// * using statements/typedefs only in namespaces
// * pure virtual methods must be one line
// * no structs/classes with the same name e.g. ns1::S1 and ns2::S1
// * for now, templates only for classes
// * DON'T put 'struct::' with a struct member e.g. struct S{ int S::fun(){return 0;} };

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
#include <unordered_map>

using std::vector;
using std::string;

namespace util
{
    struct Match
    {
        int position;
        string str;
    };

    bool startsWith(const string& s, const string& start)
    {
        return s.find(start) == 0;
    }

    template <typename T>
    T firstOrDefault(const vector<T>& v, std::function<bool(T)> pred)
    {
        for (int i = 0; i < v.size(); i++)
            if (pred(v.at(i)))
                return v.at(i);

        if (std::is_pointer<T>::value)
            return nullptr;
        else
            return T();
    }

    template <typename K, typename V>
    bool contains(const std::unordered_map<K, V>& umap, const K& key)
    {
        try
        {
            auto& val = umap.at(key);
            return true;
        }
        catch (std::out_of_range&)
        {
            return false;
        }
    }

    // returns first match of 'regex' in s
    // if there is no match, it returns (-1, "")
    Match firstMatch(const string& s, const string& regex)
    {
        std::smatch result;
        std::regex r(regex);

        std::regex_search(s, result, r);

        if (result.size() == 0)
            return{ -1, "" };
        else
            return{ result.position(0), result[0] };
    }

    void syntaxError(int line, const string& filename, const char* msg)
    {
        std::stringstream str;
        str << "Syntax error " << filename << ":" << line;
        if (msg != 0)
            str << " " << msg;

        throw std::runtime_error(str.str().c_str());
    }

    string trim(const string& str)
    {
        if (str.length() == 0)
            return str;

        string newstr(str);

        if (newstr.size() != str.size())
            throw std::runtime_error("crow says FUCK YOU");

        while (newstr.size() > 0 && newstr.back() == ' ')
            newstr.pop_back();

        int start;
        for (start = 0; start < (int)newstr.length() && str.at(start) == ' '; start++)
        {
        }

        return newstr.substr(start);
    }

    string removeLineComment(const string& s)
    {
        auto comment = util::firstMatch(s, "\\s*//");

        if (comment.position == -1)
            return s;

        return s.substr(0, comment.position);
    }

    bool endsWith(const string& s, const string& end)
    {
        if (s.length() < end.length())
            return false;

        return s.rfind(end) == (s.length() - end.length());
    }

    bool IsMethodProto(const string& str)
    {
        return (str.back() == ')' || util::endsWith(str, "const") || util::endsWith(str, "override"));
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

        virtual const string& GetProto()
        {
            throw std::runtime_error("GetProto() used without override");
        };
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
        int GetNameIndex(const string& proto)
        {
            // find first '('
            int index = proto.find('(');
            // find id
            auto id = util::firstMatch(proto, "([~_a-zA-Z0-9]+\\s*\\()|( operator[^_a-zA-Z0-9])");

            if (util::startsWith(id.str, " operator"))
                return id.position + 1;

            if (id.position == -1)
            {
                string msg = "BaseFunc::nameIndex() no id found before '(' in " + prototype;
                throw std::runtime_error(msg.c_str());
            }

            return id.position;
        }
    };

    class Method : public IDump, public BaseFunc
    {
    private:
        std::string initializerList;
        string structName;
    public:
        Method(const string& ns, const string& _struct) 
            :BaseFunc(ns), structName(_struct)
        {
        }

        // separate prototype from initializer list
        void SplitProto()
        {
            int openparencounter = 0;

            // for loop looks for a single ':' (not ::) thats outside of any parenthases
            for(int i=0;i<(int)prototype.size();i++)
            {
                if (prototype.at(i) == '(')
                    openparencounter++;
                else if (prototype.at(i) == ')')
                    openparencounter--;

                if (prototype.at(i) == ':' && openparencounter == 0 && prototype.at(i + 1) != ':')
                {
                    initializerList = prototype.substr(i);
                    prototype = prototype.substr(0, i);
                    break;
                }
                else if (prototype.at(i) == ':' && prototype.at(i + 1) == ':') // skipp ::
                    i++;
            }

            prototype = util::trim(prototype);
        }

        const string& GetProto() override
        {
            return prototype;
        }

        void Dump(std::ostream& header, std::ostream& source) override
        {
            // prototype in header
            header << prototype << ';' << std::endl;
            header << std::endl;

            // implementation in source
            // insert namespace
            string implProto(prototype);

            // remove override from impl
            int overridePos = implProto.find("override");
            if (overridePos != string::npos)
                implProto = implProto.replace(overridePos, 8, "");

            // remove static from impl
            if (util::startsWith(implProto,"static "))
                implProto = implProto.replace(0, 7, "");
            
            implProto.insert(GetNameIndex(implProto), structName + "::");

            source << "namespace " << _namespace << "{" << std::endl;
            source << implProto << initializerList << std::endl;
            source << body << "}" << std::endl;
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
                header << prototype << ";}" << std::endl;
                header << std::endl;
                
                source << "namespace " << _namespace << " {" << std::endl;
                source << prototype << std::endl;
                source << body << "}" << std::endl;
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
        Field(const string& proto):
            prototype(proto)
        {
        }

        const string& GetProto() override
        {
            return prototype;
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
        string value; // in case of initialized variables

        // splits decl and init if there is '='
        void SplitDeclaration()
        {
            int equalpos = prototype.find('=');

            if (equalpos == string::npos)
                return;

            value = prototype.substr(equalpos + 1);
            prototype = prototype.substr(0, equalpos);
        }
    public:
        NsVariable(const string& proto, const string& ns):
            prototype(proto), _namespace(ns)
        {
            SplitDeclaration();
        }

        int GetNameIndex() const
        {
            auto name = util::firstMatch(prototype, "[_a-z0-9A-Z]+;");
            return name.position;
        }

        void Dump(std::ostream& header, std::ostream& source) override
        {
            header << "namespace " << _namespace << " {" << std::endl;
            header << "    extern " << prototype;

            if (value.length() != 0)
                header << ";";

            header << "}" << std::endl;
            header << std::endl;

            source << "namespace " << _namespace << " {" << std::endl;
            source << prototype;

            if (value.length() != 0)
                source << '=' << value;

            source << "}" << std::endl;
            source << std::endl;
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
            int whereNameStarts = string("enum class ").length();
            string protostartingwithname(prototype.substr(whereNameStarts));

            auto match = util::firstMatch(protostartingwithname, "[_a-zA-Z0-9]+");
            name = match.str;
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
            header << "namespace " << _namespace << "{" << std::endl;
            header << "    " <<prototype << "}" << std::endl;
            header << std::endl;
        }
    };

    /*enum class SCType
    {
        Struct, Class
    };*/

    enum class NodeColor
    {
        White, Gray, Black
    };

    class StructClass : public IDump
    {
    protected:
        string _template;
        string prototype;
        string name;
        string _namespace;
        vector<IDump*> members; // outside private/public
        vector<IDump*> privateMembers;
        vector<IDump*> publicMembers;
        vector<IDump*> protectedMembers;

        NodeColor color;
        vector<StructClass*> dependencies;

        void FindDependenciesIn(const std::unordered_map<string, StructClass*>& structClasses, vector<IDump*>& v)
        {
            for (IDump* i : v)
            {
                string proto = i->GetProto();

                // fields, actually only fields have to be resolved
                if (proto.back() == ';')
                {
                    // get rid off identifier
                    {
                        auto id = util::firstMatch(proto, "[_a-zA-Z0-9]+;");

                        if (id.position == -1)
                        {
                            string msg = "FindDependencies() could not find id of a variable: [" + proto + "] in struct " + _namespace + "::" + name;
                            throw std::runtime_error(msg.c_str());
                        }

                        proto = proto.substr(0, id.position);
                    }

                    // search all ids without * or & at the end then check the hashtable for dependencies
                    util::Match m;

                    while ((m = util::firstMatch(proto, "([_a-zA-Z0-9]+::)*([_a-zA-Z0-9]+)( )*[&\\*]?")).position != -1)
                    {
                        // remove result
                        proto = proto.replace(m.position, m.str.length(), "");
                        m.str = util::trim(m.str);

                        // is followed by * or &
                        if (m.str.back() == '*' || m.str.back() == '&')
                            continue;

                        auto it = structClasses.find(m.str);

                        if (it != structClasses.end())
                            dependencies.push_back(it->second);
                    }
                }
                // methods, no need :D
                /*else if (util::IsMethodProto(proto))
                {
                }*/

            }
        }
    public:
        StructClass(const string& proto, const string& ns, const string& templ):
            prototype(proto), _namespace(ns), color(NodeColor::White), _template(templ)
        {
            auto match = util::firstMatch(prototype, " [_a-zA-Z0-9]+");
            name = match.str.substr(1); // substr(1) because it starts with space
        }

        const string& GetName() const
        {
            return name;
        }

        // to find dependencies find all words that dont end with & or * and that ar not identifiers or keywords
        // example: s id1;  ->   dependends on s
        //          s* id2; ->   no dependency
        //          struct S : public S2 -> depends on S2
        //          void   fun1(const s& _s1, Fish f); -> depends on Fish
        void FindDependencies(const std::unordered_map<string, StructClass*>& structClasses)
        {
            int colonPos = prototype.find(':');            

            // find inheritance dependencies
            if (colonPos != string::npos)
            {
                string str = prototype.substr(colonPos);
                util::Match m;

                while ((m = util::firstMatch(str, "[_a-zA-Z0-9]+")).position != -1)
                {
                    str = str.substr(m.position + m.str.length());
                    auto it = structClasses.find(m.str);

                    if (it != structClasses.end())
                        dependencies.push_back(it->second);
                }
            }

            FindDependenciesIn(structClasses, members);
            FindDependenciesIn(structClasses, privateMembers);
            FindDependenciesIn(structClasses, protectedMembers);
            FindDependenciesIn(structClasses, publicMembers);
        }

        void Traverse(const std::unordered_map<string, StructClass*>& structClasses, vector<StructClass*>& ordered)
        {
            if (color == NodeColor::Gray)
                throw std::runtime_error("dependency loop detected");
            else if (color == NodeColor::Black)
                return;

            color = NodeColor::Gray;

            for (auto& e : dependencies)
                e->Traverse(structClasses, ordered);

            color = NodeColor::Black;
            ordered.push_back(this);
        }

        // return struct or class
        /*SCType GetType() const
        {
            if (util::startsWith(prototype, "struct"))
                return SCType::Struct;
            else
                return SCType::Class;
        }*/

        // simple prototype is just class/struct + name (no inheritance part)
        string GetSimplePrototype() const
        {
            if (util::startsWith(prototype, "class"))
                return "class " + name;
            else if (util::startsWith(prototype, "struct"))
                return "struct " + name;
            else
                return "union " + name;
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
            
            if(_template.length() != 0)
                header << _template << std::endl;

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

    class Monolith
    {
    private:        
        vector<string> includes;
        vector<string> flags; // for conditional file parsing
        std::unordered_map<string,StructClass*> structClasses;
        vector<Function*> functions;
        vector<NsVariable*> variables;
        vector<EnumClass*> enums;
        vector<Using*> usings;
        Function* main;
        vector<StructClass*> orderedStructClasses;
        
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

            if (currentNamespace.length() > 0)
            {
                currentNamespace.pop_back();
                currentNamespace.pop_back();
            }
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
            fun->AddBody("\n");

            do
            {
                if (next(line))
                    util::syntaxError(lineNum, filename, "unexpected EOF");

                fun->AddBody(line);
                fun->AddBody("\n");

                openBrace += std::count(line.begin(), line.end(), '{');
                openBrace -= std::count(line.begin(), line.end(), '}');
            } while (openBrace > 0); // keep going until matching closing brace

            return fun;
        }

        Method* ExtractMethod(string& line, const string& structName)
        {
            Method* method = new Method(currentNamespace, structName);

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
            method->AddBody("\n");

            do
            {
                if (next(line))
                    util::syntaxError(lineNum, filename, "unexpected EOF");

                method->AddBody(line);
                method->AddBody("\n");

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

        StructClass* ExtractStructClass(const string& prototype, const string& templ)
        {
            StructClass* structClass = new StructClass(prototype, currentNamespace, templ);
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
                else if (util::startsWith(line, "/*"))
                {
                    while (!util::endsWith(line, "*/"))
                        next(line);
                }       
                else if (util::endsWith(line, ";"))
                {
                    Field* field = new Field(line);
                    structClass->AddMember(field, accSpecifier);
                }
                else if (util::endsWith(line, ")") || util::endsWith(line, ",") || util::endsWith(line, "const") || util::endsWith(line, "override"))
                {
                    Method* m = ExtractMethod(line, structClass->GetName());
                    structClass->AddMember(m, accSpecifier);
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
            string templ;

            // match '{'
            next(line);

            if (line != "{")
                util::syntaxError(lineNum, filename, "{ expected");

            while (true)
            {
                next(line);
                line = util::removeLineComment(line);
                templ = "";

                if (util::startsWith(line, "using") || util::startsWith(line, "typedef"))
                {
                    Using* u = new Using(line, currentNamespace);
                    usings.push_back(u);
                }
                else if (util::startsWith(line, "template"))
                {
                    templ = line;
                }
                else if (util::startsWith(line, "/*"))
                {
                    while (!util::endsWith(line, "*/"))
                        next(line);
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
                else if (util::startsWith(line, "enum class"))
                {                    
                    EnumClass* e = ExtractEnumClass(line);
                    enums.push_back(e);
                }
                else if (util::startsWith(line, "class") || util::startsWith(line, "struct") || util::startsWith(line, "union"))
                {
                    StructClass* s = ExtractStructClass(line, templ);

                    // structs with the same name are not allowed
                    if(util::contains<string,StructClass*>(structClasses, s->GetName()))
                        throw std::runtime_error("structs with the same name are not allowed");

                    structClasses[s->GetName()] = (s);
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
                {
                    for (const string& s : includes)
                        if (s == line)
                            continue;

                    includes.push_back(line);
                }
                else if (util::startsWith(line, "#pragma comment") || util::startsWith(line, "#define"))
                {
                    includes.push_back(line);
                }
                else if (util::startsWith(line, "#pragma compileif"))
                {
                    if (lineNum != 1)
                        throw std::runtime_error("#pragma compileif must be on the first line");
                    else
                    {
                        string flag = line.substr(18);
                        bool flagdefined = false;
                        for (int i = 0; i < (int)flags.size(); i++)
                            if (flags.at(i) == flag)
                                flagdefined = true;

                        if (!flagdefined)
                            return;
                    }
                }
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
            // dont put any code here
        }

        /////////////////// parser functions end

        void Collect(const string& filename)
        {
            this->filename = filename;
            this->lineNum = 0;
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
            // 1. find dependencies
            for (auto& sc : structClasses)
                sc.second->FindDependencies(structClasses);

            // 2. start all dependencies traversal
            // if there are no dependencies then just copy to ordered
            // if there is then do DFS to the bottom and copy on the way back
            // if there is a cycle then throw exception
            for (auto& sc : structClasses)
                sc.second->Traverse(structClasses, orderedStructClasses);
        }
    public:
        // ctor is the main driver, it will produce IR of all C++ source files
        Monolith(const vector<string>& filenames, const vector<string>& _flags):
            lineNum(0), flags(_flags), main(nullptr)
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
            if(main != nullptr)
                main->Dump(header, source);

            Dump2(header, source);
        }

        void DumpForwardDeclaration(std::ostream& header)
        {
            for (StructClass* sc : orderedStructClasses)
                sc->DumpForwardDecl(header);
        }

        void Dump2(std::ostream& header, std::ostream& source)
        {
            for (IDump* i : usings)
                i->Dump(header, source);

            header << std::endl;

            for (IDump* i : enums)
                i->Dump(header, source);

            for (IDump* i : orderedStructClasses)
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

    string headerFile;
    string sourceFile;
    string hfile; // what to #include in source file
    vector<string> files;
    vector<string> flags;

    try
    {
        for (int i = 0; i < (int)args.size(); i++)
        {
            if (args.at(i) == "-s")
            {
                sourceFile = args.at(i + 1);
                i++;
            }
            else if (args.at(i) == "-h")
            {
                headerFile = args.at(i + 1);
                i++;
            }
            else if (args.at(i) == "-f")
            {
                flags.push_back(args.at(i + 1));
                i++;
            }
            else if (args.at(i) == "-hname")
            {
                hfile = args.at(i + 1);
                i++;
            }
            else
                files.push_back(args.at(i));
        }        
    }
    catch (std::runtime_error&)
    {
        printf("problem with cmd line args\n");
        exit(0);
    }

    if (files.size() == 0)
    {
        printf("no source files\n");
        exit(0);
    }

    try
    {
        monolith::Monolith mono(files, flags);
        mono.Dump(std::ofstream(headerFile), std::ofstream(sourceFile), hfile);
        //mono.Dump(std::cout, std::cout, "header.h");
    }
    catch (std::exception& e)
    {
        printf("%s\n", e.what());
    }

    return 0;
}
