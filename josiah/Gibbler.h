#pragma once

#include <map>
#include <vector>

#include "FeatureFunction.h"
#include "GibbsOperator.h"
#include "ScoreComponentCollection.h"

namespace Moses {

class GibbsOperator;
class Hypothesis;
class TranslationOptionCollection;
class TranslationOption;
class Word;
class FeatureFunction;

  
class Sample {
 private:
  std::vector<Word> m_targetWords;
  std::vector<FeatureFunction*> m_featureFunctions;
  int source_size;
  Hypothesis* target_head;
  Hypothesis* target_tail;

  Hypothesis* source_head;
  Hypothesis* source_tail;

  ScoreComponentCollection feature_values;
  std::vector<Hypothesis*> cachedSampledHyps;
  
  std::map<size_t, Hypothesis*>  sourceIndexedHyps;
  void SetSourceIndexedHyps(Hypothesis* h);
  void UpdateFeatureValues(const ScoreComponentCollection& deltaFV);
  void UpdateTargetWordRange(Hypothesis* hyp, int tgtSizeChange);   
  void UpdateHead(Hypothesis* currHyp, Hypothesis* newHyp, Hypothesis *&head);
  void UpdateCoverageVector(Hypothesis& hyp, const TranslationOption& option) ;  
  Hypothesis* CreateHypothesis( Hypothesis& prevTarget, const TranslationOption& option);
  
  void SetTgtNextHypo(Hypothesis*  newHyp, Hypothesis* currNextHypo);
  void SetSrcPrevHypo(Hypothesis*  newHyp, Hypothesis* srcPrevHypo);
  void UpdateTargetWords();
  
 public:
  Sample(Hypothesis* target_head);
  ~Sample();
  int GetSourceSize() const { return source_size; }
  Hypothesis* GetHypAtSourceIndex(size_t ) ;
  const Hypothesis* GetSampleHypothesis() const {
    return target_head;
  }
  
  const Hypothesis* GetTargetTail() const {
    return target_tail;
  }
  
  const ScoreComponentCollection& GetFeatureValues() const {
    return feature_values;
  }
  
  void FlipNodes(size_t x, size_t y, const ScoreComponentCollection& deltaFV) ;
  void FlipNodes(const TranslationOption& , const TranslationOption&, Hypothesis* , Hypothesis* , const ScoreComponentCollection& deltaFV);
  void ChangeTarget(const TranslationOption& option, const ScoreComponentCollection& deltaFV); 
  void MergeTarget(const TranslationOption& option, const ScoreComponentCollection& deltaFV);
  void SplitTarget(const TranslationOption& leftTgtOption, const TranslationOption& rightTgtOption,  const ScoreComponentCollection& deltaFV);
  /** Words in the current target */
  const std::vector<Word>& GetTargetWords() const
  {return m_targetWords;}
  /** Extra feature functions - not including moses ones */
  const std::vector<FeatureFunction*>& GetFeatureFunctions() const
  {return m_featureFunctions;}
  
};

/**
 * Used by the operators to collect samples, for example to count ngrams, or just to print
 * them out. 
 **/
class SampleCollector {
  public:
    virtual void collect(Sample& sample) = 0;
    virtual ~SampleCollector() {}
};

class PrintSampleCollector  : public virtual SampleCollector {
  public:
    virtual void collect(Sample& sample);
    virtual ~PrintSampleCollector() {}
};

class Sampler {
 private:
   std::vector<SampleCollector*> m_collectors;
   std::vector<GibbsOperator*> m_operators;
   size_t m_iterations;
   size_t m_burninIts;
 public:
  Sampler(): m_iterations(10) {}
  void Run(Hypothesis* starting, const TranslationOptionCollection* options) ;
  void AddOperator(GibbsOperator* o) {m_operators.push_back(o);}
  void AddCollector(SampleCollector* c) {m_collectors.push_back(c);}
  void SetIterations(size_t iterations) {m_iterations = iterations;}
  void SetBurnIn(size_t burnin_its) {m_burninIts = burnin_its;}
};



}



