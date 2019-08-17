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

using namespace	std;
using namespace	icu;
using namespace	folia;

const string ISO_SET = "http://raw.github.com/proycon/folia/master/setdefinitions/iso639_3.foliaset";

const double OOV_SCORE = -50;
const char PUNCT [] = { ' ', '.', ',', ':',';','@','/','\\', '\'', '"', '(',')','[',']','{','}' };
const int punctcount = 16;

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
        for (int j = 0; j < punctcount; j++) {
            if (text[i] == PUNCT[j]) {
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

    double test(const vector<string> & tokens) {
        /* Returns a logprob*/
        double score = 0; //score to be maximised (logprob)

        for (auto& token: tokens) {
            const ::Pattern pattern = encoder->buildpattern(token, true);
            if (pattern.unknown() || !patternmodel->has(pattern)) {
                score += OOV_SCORE; //out of vocabulary score
            } else {
                score += log(patternmodel->frequency(pattern));
            }
        }

        return score;
    }
};

void usage( const string& name ){
    cerr << "Usage: " << name << " [options] file" << endl;
    cerr << "Description: Language identification on a FoLiA document against various colibri models" << endl;
    cerr << "Options:" << endl;
    cerr << "\t--lang=<code>\t use 'code' for unindentified text." << endl;
    cerr << "\t--tags=t1,t2,..\t examine text in all <t1>, <t2> ...  nodes. (default is to use the <p> nodes)." << endl;
    cerr << "\t--all\t\t assign ALL detected languages to the result. (default is to assign the most probable)." << endl;
    cerr << "\t--casesensitive\t Case sensitive (make sure the models are trained like this too if you use this!)" << endl;
    cerr << "\t--data\t Points to the data directory containing the language models" << endl;
    cerr << "\t--class\t input text class (default: current)" << endl;
    cerr << "\t-h or --help\t this message " << endl;
    cerr << "\t-d\tDebug/verbose mode" << endl;
    cerr << "\t-V or --version\t show version " << endl;
}

void setlang( FoliaElement* e, const string& langcode, const double score ){
    // append a LangAnnotation child of class 'lan'
    KWargs args;
    args["class"] = langcode;
    args["set"] = ISO_SET;
    args["confidence"] = 1.0 - score;
    LangAnnotation *node = new LangAnnotation( args, e->doc() );
    e->replace( node );
}

void add_results( const TextContent *t, const vector<std::pair<string,double>>& results, bool doAll ) {
    //assume results is sorted!
    for ( const auto& result : results ){
        setlang( t->parent(), result.first, result.second );
        if (!doAll) {
            break;
        }
    }
}

void sort_results(vector<std::pair<string,double>>& results) {
     //sort result by score (look mom, I used a lambda expression!)
     std::sort(results.begin(), results.end(), [](const std::pair<string,int> &x, const std::pair<string,int> &y) {
        return x.second > y.second;
     });
}

vector<FoliaElement*> gather_nodes( Document *doc, const string& docName, const set<string>& tags) {
    vector<FoliaElement*> result;
    for ( const auto& tag : tags ){
      ElementType et;
      try {
        et = TiCC::stringTo<ElementType>( tag );
      }
      catch ( ... ){
#pragma omp critical (logging)
        {
            cerr << "the string '" << tag<< "' doesn't represent a known FoLiA tag" << endl;
            exit(EXIT_FAILURE);
        }
    }
    vector<FoliaElement*> v = doc->doc()->select( et, true );
#pragma omp critical (logging)
    {
        cerr << "document '" << docName << "' has " << v.size() << " " << tag << " nodes " << endl;
    }
    result.insert( result.end(), v.begin(), v.end() );
  }
  return result;
}

void processFile( vector<Model>& models,
		 const string& outDir, const string& docName,
		 const string& default_lang,
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
    doc->set_metadata( "language", default_lang );
    processor *proc = add_provenance( *doc, "colibri-folialangid", command );
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
             vector<std::pair<string,double>> results;
             for (auto& model: models) {
                 double score = model.test(tokens);
                 results.push_back(std::pair<string,double>(model.lang, score));
             }
             sort_results(results);
             add_results( t, results, doAll );
             if (debug) {
                 cerr << results[0].first << "\t" << results[0].second << "\t" << text << endl;
                 for (int i = 0; i < results.size(); i++) {
                     cerr << results[i].first << "\t" << results[i].second << "\t";
                 }
                 cerr << endl;
             }
         }
       }
    }
    doc->save(outName);
    delete doc;
}

void processTextFile( vector<Model>& models,
		 const string& docName,
		 bool lowercase,
         bool debug) {

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
             vector<std::pair<string,double>> results;
             for (auto& model: models) {
                  double score = model.test(tokens);
                  results.push_back(std::pair<string,double>(model.lang, score));
             }
             sort_results(results);
             cout << results[0].first << "\t" << results[0].second << "\t" << orig_line << endl;
             if (debug) {
                 for (int i = 0; i < results.size(); i++) {
                     cout << results[i].first << "\t" << results[i].second << "\t";
                 }
                 cout << endl;
             }
        }
    }
}

int main( int argc, const char *argv[] ) {
    TiCC::CL_Options opts( "vVhO:d", "models,classfile,class,version,help,lang:,tags:,class:,casesensitive,data:" );
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

    string lang;
    opts.extract( "lang", lang );
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
    if ( tags.empty() ){
        tags.insert( "p" );
    }

    string datadir = ".";
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


    vector<string> parts = TiCC::split_at( tagsstring, "," );
    for (const auto& modelfile : modelfiles) {
      vector<string> fields = TiCC::split_at( modelfile, "." );
      string langcode = fields[0];
      size_t found = langcode.find_last_of("/");
      if (found != string::npos) {
          //strip path
          langcode = langcode.substr(found+1);
      }
      const string classfile = fields[0] + ".colibri.cls";
      ClassEncoder * encoder = new ClassEncoder(classfile);
      UnindexedPatternModel * patternmodel = new UnindexedPatternModel(modelfile, PatternModelOptions());
      models.push_back(Model(langcode, encoder, patternmodel));
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
            processFile( models, outDir, docName, lang, tags, doAll, inputclass, command, lowercase, debug );
        } else {
            processTextFile( models, docName, lowercase, debug );
        }

    }
    return EXIT_SUCCESS;
}
