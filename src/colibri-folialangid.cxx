#include <unistd.h>
#include <string>
#include <map>
#include <vector>
#include <iostream>
#include <fstream>
#include <unordered_map>
#include <unordered_set>

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

void usage( const string& name ){
    cerr << "Usage: " << name << " [options] file" << endl;
    cerr << "Description: Language identification on a FoLiA document against various colibri models" << endl;
    cerr << "Options:" << endl;
    cerr << "--lang=<code>\t use 'code' for unindentified text." << endl;
    cerr << "--tags=t1,t2,..\t examine text in all <t1>, <t2> ...  nodes. (default is to use the <p> nodes)." << endl;
    cerr << "\t--models\t This list consists of languagecode,modelfile,classfile tuples, the tuples are semicolon separated" << endl;
    cerr << "\t--class\t class (default: current)" << endl;
    cerr << "\t-h or --help\t this message " << endl;
    cerr << "\t-V or --version\t show version " << endl;
}

void setlang( FoliaElement* e, const string& lan ){
    // append a LangAnnotation child of class 'lan'
    KWargs args;
    args["class"] = lan;
    args["set"] = ISO_SET;
    LangAnnotation *node = new LangAnnotation( args, e->doc() );
    e->replace( node );
}

void addLang( const TextContent *t, const vector<string>& lv, bool doAll ) {
    //
    // we expect something like [dutch][french]
    //
    string val;
    for ( const auto& l : lv ){
        val += l;
        if ( !doAll ) break;
        if ( &l != &lv.back() ){
          val += "|";
        }
    }
    if ( !val.empty() ){
      setlang( t->parent(), val );
    }
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
        cout << "document '" << docName << "' has " << v.size() << " " << tag << " nodes " << endl;
    }
    result.insert( result.end(), v.begin(), v.end() );
  }
  return result;
}

void processFile( const Vector<PatternModel>& tc,
		 const string& outDir, const string& docName,
		 const string& default_lang,
		 set<string> tags,
		 bool doAll,
		 const string& cls,
		 const string& command )

  cout << "process " << docName << endl;
  Document *doc = 0;
  try {
    doc = new Document( "file='" + docName + "'" );
  }
  catch (const exception& e){
#pragma omp critical (logging)
    {
      cerr << "no document: " << e.what() << endl;
    }
    return;
  }
  doc->set_metadata( "language", default_lang );
  processor *proc = add_provenance( *doc, "FoLiA-langcat", command );
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
  }
  else {
    outName += docName;
  }
  string::size_type xml_pos = outName.find(".folia.xml");
  if ( xml_pos == string::npos ){
    xml_pos = outName.find(".xml");
  }
  outName.insert( xml_pos, ".lang" );
  if ( !TiCC::createPath( outName ) ){
#pragma omp critical (logging)
    {
      cerr << "unable to open output file " << outName << endl;
      cerr << "does the outputdir exist? And is it writabe?" << endl;
    }
    exit( EXIT_FAILURE);
  }
  vector<FoliaElement*> nodes = gather_nodes( doc, docName, tags );
  for ( const auto& node : nodes ){
    const TextContent *t = 0;
    string id = "NO ID";
    try {
      id = node->id();
      t = node->text_content(cls);
    }
    catch (...){
    }
    if ( t ){
      string para = t->str();
      para = TiCC::trim( para );
      if ( para.empty() ){
	if ( verbose )
#pragma omp critical (logging)
	  {
	    cerr << "WARNING: no textcontent " << id << endl;
	  }
      }
      else {
        TiCC::to_lower( para );
        vector<string> lv = tc.get_languages( para );
        addLang( t, lv, doAll );
      }
    }
    else if ( verbose ){
#pragma omp critical (logging)
      {
	cerr << "WARNING: no textcontent " << id << endl;
      }
    }
  }
  doc->save(outName);
  delete doc;
}

int main( int argc, const char *argv[] ) {
    TiCC::CL_Options opts( "vVhO:", "models,classfile,class,version,help,lang:,tags:,class:" );
    try {
        opts.init( argc, argv );
    }
    catch( TiCC::OptionError& e ){
        cerr << e.what() << endl;
        usage(argv[0]);
        exit( EXIT_FAILURE );
    }

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
    string command = "colibri-folialangid " + opts.toString();




    opts.extract( "lang", lang );
    opts.extract( "class", inputclass );
    opts.extract( 'O', outDir );

    set<string> tags;
    string tagsstring;
    opts.extract( "tags", tagsstring );
    if ( !tagsstring.empty() ){
        vector<string> parts = TiCC::split_at( tagsstring, "," );
        for( const auto& t : parts ){
          tags.insert( t );
        }
    }
    if ( doStrings ){
        if ( !tags.empty() ){
          cerr << "--tags and -s conflict." << endl;
          exit(EXIT_FAILURE);
        } else {
           tags.insert( "str" );
       }
    }
    if ( tags.empty() ){
        tags.insert( "p" );
    }

    string models_list;
    opts.extract( "models", models_list );
    //TODO: parse

    vector<string> fileNames = opts.getMassOpts();
    if ( fileNames.empty() ){
        cerr << "missing input file(s)" << endl;
        exit( EXIT_FAILURE );
    } else if ( fileNames.size() == 1 ){
        string name = fileNames[0];
        try {
            fileNames = TiCC::searchFilesMatch( name, "*.xml*", false );
        } catch ( ... ){
            cerr << "no matching xml file found: '" << name << "'" << endl;
            exit( EXIT_FAILURE );
        }
    }

    size_t toDo = fileNames.size();
    if ( toDo > 1 ){
        cout << "start processing of " << toDo << " files " << endl;
    }
    for ( size_t fn=0; fn < toDo; ++fn ){
        string docName = fileNames[fn];
        processFile( TC, outDir, docName, lang, tags, cls, command );
    }
    return EXIT_SUCCESS;
}
