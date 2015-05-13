/*
 * HwcmSScorer.cpp
 *
 *  Created on: May 5, 2015
 *      Author: mnadejde
 */

#include "HwcmSScorer.h"
#include "ScoreStats.h"
#include "moses/Util.h"
#include <fstream>


// HWCM score (Liu and Gildea, 2005). Implements F1 instead of precision for better modelling of hypothesis length.
// assumes dependency trees on target side (generated by scripts/training/wrappers/conll2mosesxml.py ; use with option --brackets for reference).
// reads reference trees from separate file {REFERENCE_FILE}.trees to support mix of string-based and tree-based metrics.

using namespace std;
//using boost::unordered::unordered_map;

namespace MosesTuning
{

HwcmSScorer::HwcmSScorer(const string& config)
  : StatisticsBasedScorer("HWCMS",config) {
	using Moses::TokenizeMultiCharSeparator;
	m_currentRefId = -1;
	m_includeRel = false;
	m_order = 3;
	m_totalRef.assign(m_order,0);
/*	string jarPath= "/Users/mnadejde/Documents/workspace/stanford-parser-full-2014-08-27/stanford-parser-3.4.1-models.jar";
	jarPath+=":/Users/mnadejde/Documents/workspace/stanford-parser-full-2014-08-27/stanford-parser.jar";
	jarPath+=":/Users/mnadejde/Documents/workspace/stanford-parser-full-2014-08-27/commons-lang3-3.3.2.jar";
	jarPath+=":/Users/mnadejde/Documents/workspace/moses_010914/mosesdecoder/Relations/Relations.jar";
*/
	vector<string> parameters = TokenizeMultiCharSeparator(config,",");
	javaWrapper = Moses::CreateJavaVM::Instance(parameters[0]);
	//Create object
	JNIEnv *env =  javaWrapper->GetAttachedJniEnvPointer();

	env->ExceptionDescribe();

	jobject rel = env->NewObject(javaWrapper->GetRelationsJClass(), javaWrapper->GetDepParsingInitJId());
	env->ExceptionDescribe();
	jmethodID	initLP = 	env->GetMethodID(javaWrapper->GetRelationsJClass(), "InitLP","()V");
	env ->CallObjectMethod(rel,initLP);
	env->ExceptionDescribe();

	m_workingStanforDepObj = env->NewGlobalRef(rel);
	env->DeleteLocalRef(rel);
	javaWrapper->GetVM()->DetachCurrentThread();


}

HwcmSScorer::~HwcmSScorer() {
	JNIEnv *env =  javaWrapper->GetAttachedJniEnvPointer();
	env->ExceptionDescribe();
	if (!env->ExceptionCheck()){
		env->DeleteLocalRef(m_workingStanforDepObj);
	}
}


std::string HwcmSScorer::CallStanfordDep(std::string parsedSentence, jmethodID methodId) const{

	JNIEnv *env =  javaWrapper->GetAttachedJniEnvPointer();
	env->ExceptionDescribe();


	/**
	 * arguments to be passed to ProcessParsedSentenceJId:
	 * string: parsed sentence
	 * boolean: TRUE for selecting certain relations (list them with GetRelationListJId method)
	*/

	jstring jSentence = env->NewStringUTF(parsedSentence.c_str());
	jboolean jSpecified = JNI_TRUE;
	env->ExceptionDescribe();


	/**
	 * Call this method to get a string with the selected dependency relations
	 * Issues: method should be synchronize since sentences are decoded in parallel and there is only one object per feature
	 * Alternative should be to have one object per sentence and free it when decoding finished
	 */
	if (!env->ExceptionCheck()){
		//it's the same method id
		//VERBOSE(1, "CALLING JMETHOD ProcessParsedSentenceJId: " << env->GetMethodID(javaWrapper->GetRelationsJClass(), "ProcessParsedSentence","(Ljava/lang/String;Z)Ljava/lang/String;") << std::endl);
		jstring jStanfordDep = reinterpret_cast <jstring> (env ->CallObjectMethod(m_workingStanforDepObj,methodId,jSentence,jSpecified));

		env->ExceptionDescribe();

		//Returns a pointer to a UTF-8 string
		if(jStanfordDep != NULL){
		const char* stanfordDep = env->GetStringUTFChars(jStanfordDep, 0);
		std::string dependencies(stanfordDep);

		//how to make sure the memory gets released on the Java side?
		env->ReleaseStringUTFChars(jStanfordDep, stanfordDep);
		env->DeleteLocalRef(jSentence);

		env->ExceptionDescribe();
		javaWrapper->GetVM()->DetachCurrentThread();
		return dependencies;
		}
		std::cerr<< "jStanfordDep is NULL" << std::endl;

		//this would be deleted anyway once the thread detaches?
		if(jSentence!=NULL){
			env->DeleteLocalRef(jSentence);
			env->ExceptionDescribe();
		}

		javaWrapper->GetVM()->DetachCurrentThread(); //-> when jStanfordDep in null it already crashed?

		return "null";
	}
	javaWrapper->GetVM()->DetachCurrentThread();


	return "exception";
}

void HwcmSScorer::setReferenceFiles(const vector<string>& referenceFiles)
{
  if (referenceFiles.size() != 1) {
    throw runtime_error("HWCM only supports a single reference");
  }
  ifstream inRef((referenceFiles[0]).c_str());
  ifstream inDep((referenceFiles[0] + ".dep").c_str());
  if (!inDep) {
    throw runtime_error("Unable to open " + referenceFiles[0] + ".dep");
  }
  if (!inRef) {
      throw runtime_error("Unable to open " + referenceFiles[0]);
    }
  string lineRef, lineDep;
  while (getline(inRef,lineRef) && getline(inDep,lineDep)) {
  	lineRef = this->preprocessSentence(lineRef);
    lineDep = this->preprocessSentence(lineDep);
    m_ref.push_back(pair<string,string>(lineRef,lineDep));
  }
}

vector <map <string,int> > HwcmSScorer::MakeTuples(string sentence, string dep, size_t order, vector<int> &totals){
	using Moses::Tokenize;

	//cout<<ref<<endl;
	//cout<<dep<<endl;

	vector<string> words;
	vector<string> dependencies;
	totals.assign(order,0);

	//map <string,int> dependencyChains;
	vector< map <string,int> > dependencyChains(order);
	map <int,pair<int,string> > dependencyTuples;
	pair<map <string,int>::iterator,bool> itChains;
	pair<map <int, pair<int,string> >::iterator,bool > itTuples;
	Tokenize(words,sentence);
	Tokenize(dependencies,dep);
	words.insert(words.begin(),"ROOT");

	int child,head;
	for(size_t i=0; i<dependencies.size();i=i+3){
		child = strtol (dependencies[i].c_str(),NULL,10);
		head = strtol (dependencies[i+1].c_str(),NULL,10);
		itTuples = dependencyTuples.insert(pair<int, pair<int,string> >(child,pair<int,string>(head,dependencies[i+2])));
	}
	for(map <int,pair<int,string> >::iterator it=dependencyTuples.begin();it!=dependencyTuples.end();it++){
		map <int,pair<int,string> >::iterator itHistory = it;
		string key = words[itHistory->first];
		for(size_t i=0;i<order;i++){
			key += " " + words[itHistory->second.first];
			if(m_includeRel)
				key += " " +itHistory->second.second;
			itChains = dependencyChains[i].insert(pair<string,int> (key,1));
			if(itChains.second==false){
				//tuple already seen -> increase count
				itChains.first->second+=1;
			}
			totals[i]++;
			//cout<<itChains.first->first<<" "<<itChains.first->second<<" ";
			itHistory=dependencyTuples.find(itHistory->second.first);
			if(itHistory==dependencyTuples.end())
				break;
		}

	}

	//cout<<endl;
	return dependencyChains;
}

void HwcmSScorer::prepareStats(std::size_t sid, const std::string& text, ScoreStats& entry){
	using Moses::TokenizeMultiCharSeparator;
  if (sid >= m_ref.size()) {
    stringstream msg;
    msg << "Sentence id (" << sid << ") not found in reference set";
    throw runtime_error(msg.str());
  }

  //text is processed by loadNBest which reads in the translation and the alignment field if the UseAlignment() says so
  //instead of alignment I have the dependency tuples -> but I might want to print the trees then "alignment" field will be the 6th
  string sentence = this->preprocessSentence(text);
  vector<string> dependencies = TokenizeMultiCharSeparator(sentence,"|||");
  string depRel = CallStanfordDep(dependencies[0],javaWrapper->GetProcessSentenceJId());
  vector <map<string,int> > nbestTuples;
  vector<int> totalNbest, totalRef;
  vector<int> stats;
  stats.assign(m_order*3,0);
  if(dependencies.size()>0){
  	//cout<<depRel<<endl;
  	//cout<<dependencies[1]<<endl;
  	nbestTuples = MakeTuples(dependencies[0], depRel,m_order, totalNbest);
  	if(m_currentRefId!=sid){
  		m_currentRefId=sid;
  		m_currentRefTuples = MakeTuples(m_ref[sid].first, m_ref[sid].second,m_order, totalRef);
  		m_totalRef=totalRef;
  	}

		map <string,int>::iterator itNbest,itRef;
		for(size_t i=0;i<m_order;i++){
			int correct=0;
			for(itNbest = nbestTuples[i].begin(); itNbest!= nbestTuples[i].end(); itNbest++){
				itRef=m_currentRefTuples[i].find(itNbest->first);
				if(itRef!=m_currentRefTuples[i].end()){
					correct+=std::min(itNbest->second,itRef->second);
				}
			}
			//cout<<correct<<" "<<totalNbest[i]<<" "<<m_totalRef[i]<<endl;
			stats[i*3]=correct;
			stats[i*3+1]=totalNbest[i];
			stats[i*3+2]=m_totalRef[i];
		}
  }
  entry.set(stats);
}

float HwcmSScorer::calculateScore(const std::vector<int>& comps) const{
	float precision = 0.0;
	float recall = 0.0;
	for (size_t i = 0; i < m_order; i++) {
		float matches = comps[i*3];
		float test_total = comps[1+(i*3)];
		float ref_total = comps[2+(i*3)];
		if (test_total > 0) {
			precision += matches/test_total;
		}
		if (ref_total > 0) {
			recall += matches/ref_total;
		}
		//cout<<precision<<" "<<recall<<endl;
	}

	precision /= (float)m_order;
	recall /= (float)m_order;
	if((precision+recall)==0){
		stringstream msg;
		msg << "precision+recall==0";
		throw runtime_error(msg.str());
	}

	//cout<<precision<<" "<<recall<<" "<<(2*precision*recall)/(precision+recall)<<endl;
	return (2*precision*recall)/(precision+recall); // f1-score
}

}
