#include <unistd.h>
#include <string>
#include <map>
#include <algorithm>
#include <vector>
#include <utility>
#include <iostream>
#include <fstream>
#include <unordered_map>
#include <unordered_set>
#include <cmath>

#include "ticcutils/FileUtils.h"
#include "ticcutils/CommandLine.h"
#include "ticcutils/PrettyPrint.h"
#include "ticcutils/StringOps.h"
#include "ticcutils/Unicode.h"
#include "libfolia/folia.h"

#include "colibri-core/patternmodel.h"

#ifndef DATA_DIR
#define DATA_DIR string(SYSCONF_PATH) + "/colibri-utils/"
#endif

using namespace	std;
using namespace	icu;
using namespace	folia;

const string ISO_SET = "http://raw.github.com/proycon/folia/master/setdefinitions/iso639_3.foliaset";

const double OOV_SCORE = -50;
const set<char> PUNCT = { ' ', '.', ',', ':',';','@','/','\\', '\'', '"', '(',')','[',']','{','}','_','?','!','#','%'};


typedef PatternModel<uint32_t> UnindexedPatternModel;

folia::processor *add_provenance( folia::Document& doc,
				  const string& label,
				  const string& command ) {
    folia::processor *proc = doc.get_processor( label );
    if ( proc ){
        throw logic_error( "add_provenance() failed, label: '" + label + "' already exists." );
    }
    folia::KWargs args;
    args["name"] = label;
    args["generate_id"] = "auto()";
    args["version"] = PACKAGE_VERSION;
    args["command"] = command;
    args["begindatetime"] = "now()";
    args["generator"] = "yes";
    proc = doc.add_processor( args );
    proc->get_system_defaults();
    return proc;
}

vector<string> tokenise(const string & text) {
    //we are going to have to do some very crude tokenisation here
    bool ispunct;
    string token = "";
    vector<string> tokens;
    for (size_t i = 0; i < text.size(); i++) {
        ispunct = false;
        for (const auto& punct: PUNCT) {
            if (text[i] == punct) {
                ispunct = true;
                break;
            }
        }
        if (ispunct) {
            if (!token.empty()) {
                tokens.push_back(token);
                token = "";
            }
        } else {
            token += text[i];
        }
    }
    if (!token.empty()) {
        tokens.push_back(token);
        token = "";
    }
    return tokens;
}

class Model {
   public:
    string lang;
    UnindexedPatternModel * patternmodel;
    ClassEncoder * encoder;

    Model(const string & lang,  ClassEncoder * encoder, UnindexedPatternModel * patternmodel) {
        this->lang = lang;
        this->encoder = encoder;
        this->patternmodel = patternmodel;
    }

    pair<double,double> test(const vector<string> & tokens) {
        /* Returns a logprob*/
        double score = 0; //score to be maximised (logprob)
        double confidence = 0;
        int coverage = 0;

        for (auto& token: tokens) {
            const ::Pattern pattern = encoder->buildpattern(token, true);
            if (pattern.unknown() || !patternmodel->has(pattern)) {
                score += OOV_SCORE; //out of vocabulary score
            } else {
                score += log(patternmodel->frequency(pattern));
                coverage += 1;
            }
        }

        if (tokens.size() == 0) {
            score = -999999;
        } else {
            confidence = coverage / (double) tokens.size();
        }

        return make_pair(score, confidence);
    }
};

void usage( const string& name ){
    cerr << "Usage: " << name << " [options] file" << endl;
    cerr << "Description: Language identification on a FoLiA XML document or plain text document, tested against various colibri models" << endl;
    cerr << "Options:" << endl;
    cerr << "\t--casesensitive\t Case sensitive (make sure the models are trained like this too if you use this!)" << endl;
    cerr << "\t--data\t Points to the data directory containing the language models" << endl;
    cerr << "\t--lang=<code>\t fallback language, used for unindentified text." << endl;
    cerr << "\t--langs=<code>,<code>\t constrain to these languages only." << endl;
    cerr << "\t-h or --help\t this message " << endl;
    cerr << "\t-d\tDebug/verbose mode" << endl;
    cerr << "\t-V or --version\t show version " << endl;
    cerr << "Options for FoLiA processing:" << endl;
    cerr << "\t--tags=t1,t2,..\t examine text in all <t1>, <t2> ...  nodes. (default is to use all structural nodes that have text)." << endl;
    cerr << "\t--subcodes=<code>:<main>\tMap language code <code> as a variant of <main>, will be stored as a FoLiA feature with subset 'variant'. Comma separated list of colon seperated tuples. Example --subcodes=dum:nld,nld-vnn:nld" << endl;
    cerr << "\t--all\t\t assign ALL detected languages to the result. (default is to assign the most probable)." << endl;
    cerr << "\t--class\t input text class (default: current)" << endl;
    cerr << "\t--confidence\t confidence threshold (default: 0.5), if the confidence is lower the fallback language will be used (if set), or no language annotation is made at all otherwise." << endl;
}

void setlang( FoliaElement* e, const string& langcode, const double confidence, const bool alternative, const unordered_map<string,string>& subcodes){
    // append a LangAnnotation child of class 'lan'
    bool addfeature = false;
    KWargs args;
    const auto found = subcodes.find(langcode);
    if (found != subcodes.end()) {
        args["class"] = found->second;
        addfeature = true;
    } else {
        args["class"] = langcode;
    }
    args["set"] = ISO_SET;
    args["confidence"] = to_string(confidence);
    LangAnnotation *node = new LangAnnotation( args, e->doc() );
    if (addfeature) {
        KWargs args2;
        args2["subset"] = "variant";
        args2["class"] = langcode;
        Feature *feat = new Feature( args2, e->doc());
        node->append( feat);
    }
    if (alternative) {
        KWargs args2;
        Alternative *altnode = new Alternative( args2, e->doc() );
        altnode->append(node);
        e->append( altnode );
    } else {
        e->replace( node );
    }
}

void add_results( const TextContent *t, const vector<std::pair<string,pair<double,double>>>& results, bool doAll, const unordered_map<string,string>& subcodes) {
    //assume results is sorted!
    bool first = true;
    for ( const auto& result : results ){
        setlang( t->parent(), result.first, result.second.second, !first, subcodes );
        if (!doAll) {
            break;
        }
        first = false;
    }
}

void sort_results(vector<pair<string,pair<double,double>>>& results) {
     //sort result by score (look mom, I used a lambda expression!)
     std::sort(results.begin(), results.end(), [](const std::pair<string,pair<double,double>> &x, const std::pair<string,pair<double,double>> &y) {
        return x.second.first > y.second.first;
     });
}

vector<FoliaElement*> gather_nodes( Document *doc, const string& docName, const set<string>& tags) {
    vector<FoliaElement*> result;
    if (tags.empty()) {
        //no elements predefined, we grab all structural elements that have text
        vector<FoliaElement*> v = doc->doc()->select( TextContent_t, true );
        for ( const auto& t: v) {
            if ((t->parent() != NULL) && isSubClass( t->parent()->element_id(), AbstractStructureElement_t)) {
                result.push_back(t->parent());
            }
        }
    } else {
        for ( const auto& tag : tags ){
            ElementType et;
            try {
                et = TiCC::stringTo<ElementType>( tag );
            } catch ( ... ){
                cerr << "the string '" << tag<< "' doesn't represent a known FoLiA tag" << endl;
                exit(EXIT_FAILURE);
            }
            vector<FoliaElement*> v = doc->doc()->select( et, true );
            cerr << "document '" << docName << "' has " << v.size() << " " << tag << " nodes " << endl;
            result.insert( result.end(), v.begin(), v.end() );
        }
    }
    return result;
}

pair<string,double> printstats(const unordered_map<string,size_t> & stats) {
    std::vector<pair<string,size_t>> v;
    std::copy(stats.begin(), stats.end(), std::back_inserter<std::vector<pair<string,size_t>>>(v) );
    std::sort(v.begin(), v.end(), [](const std::pair<string,size_t> &x, const std::pair<string,size_t> &y) {
            return x.second > y.second;
    });
    size_t total = 0;
    for (const auto& iter: v ) { total += iter.second; }
    cout << "#DOCUMENT:\t";
    string mainlang = "";
    double mainfreq = 0.0;
    for (const auto& iter: v ) {
        if (mainlang.empty()) {
            mainlang = iter.first;
            mainfreq = iter.second / (double) total;
        }
        cout << iter.first << "\t" << iter.second / (double) total<< "\t";
    }
    cout << endl;
    return make_pair(mainlang, mainfreq / (double) total) ;
}

void processFile( vector<Model>& models,
		 const string& outDir, const string& docName,
		 const string& fallback_lang,
         const unordered_map<string,string>& subcodes,
		 const double confidence_threshold,
		 set<string> tags,
		 bool doAll,
		 const string& cls,
		 const string& command,
		 bool lowercase,
         bool debug) {

    cerr << "process " << docName << endl;
    Document *doc = 0;
    try {
        doc = new Document( "file='" + docName + "'" );
    } catch (const exception& e){
        cerr << "no document: " << e.what() << endl;
        return;
    }
    processor *proc = add_provenance( *doc, "colibri-lang", command );
    if ( !doc->declared(  AnnotationType::LANG, ISO_SET ) ){
        KWargs args;
        args["processor"] = proc->id();
        doc->declare( AnnotationType::LANG, ISO_SET, args );
    }
    string outName;
    if ( !outDir.empty() ){
        outName = outDir + "/";
    }
    string::size_type pos = docName.rfind("/");
    if ( pos != string::npos ){
       outName += docName.substr( pos+1);
    } else {
       outName += docName;
    }
    string::size_type xml_pos = outName.find(".folia.xml");
    if ( xml_pos == string::npos ){
        xml_pos = outName.find(".xml");
    }

    outName.insert( xml_pos, ".lang" );
    if ( !TiCC::createPath( outName ) ){
       cerr << "unable to open output file " << outName << endl;
       cerr << "does the outputdir exist? And is it writabe?" << endl;
       exit( EXIT_FAILURE);
    }

    unordered_map<string,size_t> stats;

    vector<FoliaElement*> nodes = gather_nodes( doc, docName, tags );
    for ( const auto& node : nodes ){
       const TextContent *t = 0;
       string id = "NO ID";
       try {
           id = node->id();
           t = node->text_content(cls);
       } catch (...){ }

       if ( t ){
         string text = t->str();
         text = TiCC::trim( text );
         if ( !text.empty() ){
             if (lowercase) {
                 text = TiCC::utf8_lowercase(text);
             }
             const vector<string> tokens = tokenise(text);
             vector<pair<string,pair<double,double>>> results;
             for (auto& model: models) {
                 pair<double,double> result = model.test(tokens);
                 results.push_back(std::pair<string,pair<double,double>>(model.lang, result));
             }
             sort_results(results);
             stats[results[0].first] += 1;
             const double logprob =  results[0].second.first;
             const double confidence =  results[0].second.second;
             if (confidence > confidence_threshold || doAll) {
                 add_results( t, results, doAll, subcodes);
             } else if (!fallback_lang.empty()) {
                 setlang(t->parent(), fallback_lang, 0.0, false, subcodes);
             }
             if (debug) {
                 cerr << results[0].first << "\t" << logprob << "\t" << confidence << text << endl;
                 for (int i = 0; i < results.size(); i++) {
                     cerr << results[i].first << "\t" << results[i].second.first << "\t" << results[i].second.second;
                 }
                 cerr << endl;
             }
         }
       }
    }
    pair<string,double> result  = printstats(stats);
    if (!result.first.empty() && result.second >= 0.66) {
        doc->set_metadata("language", result.first);
    }
    doc->save(outName);
    delete doc;
}

void processTextFile( vector<Model>& models,
		 const string& docName,
		 bool lowercase,
         bool debug) {

    unordered_map<string,size_t> stats;
    string line;
    ifstream f(docName);
    while(std::getline(f, line)) {
         line = TiCC::trim( line );
         if (line.empty()) {
             cout << endl;
         } else {
             string orig_line = line;
             if (lowercase) {
                 TiCC::to_lower(line);
             }
             const vector<string> tokens = tokenise(line);
             vector<std::pair<string,pair<double,double>>> results;
             for (auto& model: models) {
                  pair<double,double> result = model.test(tokens);
                  results.push_back(std::pair<string,pair<double,double>>(model.lang, result));
             }
             sort_results(results);
             stats[results[0].first] += 1;
             const double logprob =  results[0].second.first;
             const double confidence =  results[0].second.second;
             cout << results[0].first << "\t" << logprob << "\t" << confidence << "\t" << orig_line << endl;
             if (debug) {
                 for (int i = 0; i < results.size(); i++) {
                     cout << results[i].first << "\t" << results[i].second.first << "\t" << results[i].second.second << "\t";
                 }
                 cout << endl;
             }
        }
    }
    printstats(stats);
}

int main( int argc, const char *argv[] ) {
    TiCC::CL_Options opts( "vVhO:d", "models,classfile,class,version,help,lang:,langs:,tags:,class:,casesensitive,data:,threshold:,confidence:,subcodes:" );
    try {
        opts.init( argc, argv );
    } catch( TiCC::OptionError& e ){
        cerr << e.what() << endl;
        usage(argv[0]);
        exit( EXIT_FAILURE );
    }

    const string progname = opts.prog_name();
    string inputclass = "current";
    string outDir;

    if ( opts.extract( 'h' ) || opts.extract( "help" ) ){
        usage(progname);
        exit(EXIT_SUCCESS);
    }
    if ( opts.extract( 'V' ) || opts.extract( "version" ) ){
        cerr << PACKAGE_STRING << endl;
        exit(EXIT_SUCCESS);
    }
    string command = progname + " " + opts.toString();


    const bool lowercase = !opts.extract("casesensitive");
    const bool doAll = opts.extract( "all" );
    const bool debug = opts.extract('d');

    string fallback = ""; //no fallback by default
    double confidence_threshold = 0.5;
    opts.extract( "lang", fallback );
    opts.extract( "confidence", confidence_threshold );
    opts.extract( "class", inputclass );
    opts.extract( 'O', outDir );

    set<string> tags;
    string tagsstring;
    opts.extract( "tags", tagsstring );
    if ( !tagsstring.empty() ){
        vector<string> parts = TiCC::split_at( tagsstring, "," );
        for (const auto& t : parts) {
          tags.insert( t );
        }
    }

    set<string> langs;
    string langs_string;
    opts.extract( "langs", langs_string );
    if ( !tagsstring.empty() ){
        vector<string> parts = TiCC::split_at( langs_string, "," );
        for (const auto& langcode : parts) {
          langs.insert( langcode );
        }
    }

    unordered_map<string,string> subcodes;
    string subcodes_string;
    opts.extract( "subcodes", langs_string );
    if ( !langs_string.empty() ) {
        vector<string> parts = TiCC::split_at( langs_string, "," );
        for (const auto& part : parts) {
          vector<string> parts2 = TiCC::split_at( part, ":" );
          if (parts2.size() != 2) {
            cerr << "Invalid tuple in --subcodes: '" << part << "'" << endl;
            exit( EXIT_FAILURE );
          }
          subcodes[parts2[0]] = parts2[1];
        }
    }


    string datadir = DATA_DIR;
    datadir += "data";
    vector<Model> models;
    opts.extract( "data", datadir );
    vector<string> modelfiles;
    try {
        modelfiles = TiCC::searchFilesMatch( datadir, "*.model", false );
    } catch ( ... ){
        cerr << "no data files found in '" << datadir << "'" << endl;
        exit( EXIT_FAILURE );
    }
    if (modelfiles.empty()) {
        cerr << "no data files found in '" << datadir << "'" << endl;
        exit( EXIT_FAILURE );
    }


    for (const auto& modelfile : modelfiles) {
      size_t delimiter = modelfile.find_last_of("/");
      string filename = modelfile;
      if (delimiter != string::npos) {
          filename = modelfile.substr(delimiter+1);
      }
      vector<string> fields = TiCC::split_at( filename, "." );
      string langcode = fields[0];
      if (langs.empty() || langs.find(langcode) != langs.end()) {
          const string classfile = fields[0] + ".colibri.cls";
          ClassEncoder * encoder = new ClassEncoder(classfile);
          UnindexedPatternModel * patternmodel = new UnindexedPatternModel(modelfile, PatternModelOptions());
          models.push_back(Model(langcode, encoder, patternmodel));
      }
    }

    vector<string> fileNames = opts.getMassOpts();
    if ( fileNames.empty() ){
        cerr << "missing input file(s)" << endl;
        exit( EXIT_FAILURE );
    } else if ( fileNames.size() == 1 ){
        string name = fileNames[0];
        try {
            fileNames = TiCC::searchFilesMatch( name, "*", false );
        } catch ( ... ){
            cerr << "no files found: '" << name << "'" << endl;
            exit( EXIT_FAILURE );
        }
    }

    size_t toDo = fileNames.size();
    if ( toDo > 1 ){
        cerr << "start processing of " << toDo << " files " << endl;
    }
    for ( size_t fn=0; fn < toDo; ++fn ){
        string docName = fileNames[fn];
        if (docName.substr(docName.size() - 4) == ".xml") {
            processFile( models, outDir, docName, fallback, subcodes, confidence_threshold, tags, doAll, inputclass, command, lowercase, debug );
        } else {
            processTextFile( models, docName, lowercase, debug );
        }

    }
    return EXIT_SUCCESS;
}
