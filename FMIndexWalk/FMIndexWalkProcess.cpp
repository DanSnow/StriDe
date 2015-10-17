///-----------------------------------------------
// Copyright 2015 National Chung Cheng University
// Written by Yao-Ting Huang
// Released under the GPL
//-----------------------------------------------
//
// FMIndexWalkProcess.cpp - Implementation of FM-index walk and kmerization of PE reads
//
#include "FMIndexWalkProcess.h"
#include "CorrectionThresholds.h"
#include "HashMap.h"
#include <iomanip>
#include "SAIntervalTree.h"
#include "SAIPBSelfCTree.h"
#include "SAIPBHybridCTree.h"
using namespace std;

//#define KMER_TESTING 1


FMIndexWalkProcess::FMIndexWalkProcess(const FMIndexWalkParameters params) : m_params(params)
{
}

FMIndexWalkProcess::~FMIndexWalkProcess()
{

}


FMIndexWalkResult FMIndexWalkProcess::MergeAndKmerize(const SequenceWorkItemPair& workItemPair)
{
	FMIndexWalkResult result;

	//get parameters
	size_t kmerLength = m_params.kmerLength ;
	size_t threshold = (size_t)CorrectionThresholds::Instance().getRequiredSupport(0)-1;

	std::string seqFirst  = workItemPair.first.read.seq.toString() ;
	std::string seqSecond = workItemPair.second.read.seq.toString();

	//Trim head and tail from both ends if there is low-frequency kmer
	seqFirst = trimRead(seqFirst, m_params.kmerLength, threshold, m_params.indices);
	seqSecond = trimRead(seqSecond, m_params.kmerLength, threshold, m_params.indices);
	
	if( (seqFirst.length()<=(size_t)m_params.minOverlap && seqFirst.length()>=(size_t)kmerLength) || 
		(seqSecond.length()<=(size_t)m_params.minOverlap && seqSecond.length()>=(size_t)kmerLength) )
		{
			result.kmerize =true;
			result.kmerize2 =true;
			result.correctSequence = seqFirst ;
			result.correctSequence2 = seqSecond ;
			
		}
	else if(seqFirst.length()<(size_t)kmerLength || seqSecond.length()<(size_t)kmerLength)
		return result;

	std::string firstKRstr = seqFirst.substr(0, m_params.minOverlap);
	std::string secondKRstr  = seqSecond.substr(0, m_params.minOverlap);
	if( isSuitableForFMWalk(firstKRstr, secondKRstr) )
	{	
		//maxOverlap is limited to 90% of read length which aims to prevent over-greedy search
		size_t maxOverlap = m_params.maxOverlap!=-1?m_params.maxOverlap:
		((workItemPair.first.read.seq.length()+workItemPair.second.read.seq.length())/2)*0.95;

		std::string mergedseq1, mergedseq2;
		//Walk from the 1st end to 2nd end											
		SAIntervalTree SAITree1(&firstKRstr, m_params.minOverlap, maxOverlap, m_params.maxInsertSize, m_params.maxLeaves,
		m_params.indices, reverseComplement(secondKRstr));
		SAITree1.mergeTwoReads(mergedseq1);

		//Walk from the 2nd end to 1st end using the other strand
		SAIntervalTree SAITree2(&secondKRstr, m_params.minOverlap, maxOverlap, m_params.maxInsertSize, m_params.maxLeaves,
		m_params.indices, reverseComplement(firstKRstr));

		SAITree2.mergeTwoReads(mergedseq2);
		
		//Only one successful walk from first end, requiring maxUsedLeaves <=1 in order to avoid walking over chimera PE read.
		if(!mergedseq1.empty() && mergedseq2.empty() && SAITree1.getMaxUsedLeaves()<=1 && SAITree2.getMaxUsedLeaves()<=1)
		// if(!mergedseq1.empty() && mergedseq2.empty())
		{
			// std::cout << ">" << SAITree.getKmerCoverage()<< "\n" << mergedseq << "\n" ;
			// std::cout << SAITree1.getMaxUsedLeaves() << "\t" << SAITree1.isBubbleCollapsed() << "\t" << SAITree2.getMaxUsedLeaves() << "\n";
			// getchar();
			result.merge = true ;
			result.correctSequence = mergedseq1 ;
			return result;
			//Only one successful walk from second end
		}else if( mergedseq1.empty() && !mergedseq2.empty() && SAITree2.getMaxUsedLeaves()<=1 && SAITree1.getMaxUsedLeaves() <=1)
		// }else if( mergedseq1.empty() && !mergedseq2.empty() )
		{
			result.merge = true ;
			result.correctSequence = mergedseq2 ;
			return result;
		}
		else if( !mergedseq1.empty() && !mergedseq2.empty() && ( mergedseq1 == reverseComplement(mergedseq2) )  )
		// else if( !mergedseq1.empty() && !mergedseq2.empty() )
		{
			// std::cout << ">" << SAITree.getKmerCoverage()<< "\n" << mergedseq << "\n>" << SAITree2.getKmerCoverage()<< "\n" << mergedseq2 << "\n";
			// std::cout << SAITree.getMaxUsedLeaves() << "\t" << SAITree.isBubbleCollapsed() << "\t" << SAITree2.getMaxUsedLeaves() << "\t" << SAITree2.isBubbleCollapsed()<<"\n";
			// getchar();
			result.merge = true ;
			result.correctSequence = (SAITree1.getKmerCoverage()>SAITree2.getKmerCoverage())? mergedseq1:mergedseq2 ;
			return result;
		}
	}
	
	
	/** Case 3: kmerize the remaining reads **/
	//Compute kmer freq of each kmer
	KmerContext seqFirstKC(seqFirst, kmerLength, m_params.indices);
	KmerContext seqSecondKC(seqSecond, kmerLength, m_params.indices);

	std::vector<std::string> firstKR ;
	std::vector<std::string> secondKR ;
	int firstMainIdx=-1, secondMainIdx=-1;

	if(seqFirst.length()>=(size_t) kmerLength) 
	firstMainIdx = splitRead( seqFirstKC, firstKR, threshold, m_params.indices);
	if(seqSecond.length()>=(size_t) kmerLength)
	secondMainIdx = splitRead( seqSecondKC, secondKR, threshold, m_params.indices);
	// /***trim and kmerize reads***/

	/*** write kmernized results***/
	if (!firstKR.empty()) result.kmerize =true ;
	if (!secondKR.empty()) result.kmerize2 =true ;
	
	for (int i = 0 ; i<(int)firstKR.size() ; i ++ )
	{
		std::string kmerRead = firstKR.at(i);
		float GCratio =0 ;
		if ( isLowComplexity (kmerRead,GCratio) ) continue;
		if ( maxCon(kmerRead)*3 > kmerRead.length() ) continue;
		if (i==firstMainIdx)  result.correctSequence = kmerRead ;
		else
		result.kmerizedReads.push_back(kmerRead);
	}

	for (int i = 0 ; i<(int)secondKR.size() ; i ++ )
	{
		std::string kmerRead = secondKR.at(i);
		float GCratio =0 ;
		if ( isLowComplexity (kmerRead,GCratio) ) continue;
		if ( maxCon(kmerRead)*3 > kmerRead.length() ) continue;
		if (i==secondMainIdx)  result.correctSequence2 = kmerRead ;
		else
		result.kmerizedReads2.push_back(kmerRead);
	}
	
	return result;
	
}


FMIndexWalkResult FMIndexWalkProcess::MergePairedReads(const SequenceWorkItemPair& workItemPair)
{
	FMIndexWalkResult result;

	//get parameters
	// size_t kmerLength = m_params.kmerLength ;
	size_t threshold = (size_t)CorrectionThresholds::Instance().getRequiredSupport(0)-1;

	std::string seqFirstOriginal  = workItemPair.first.read.seq.toString() ;
	std::string seqSecondOriginal = workItemPair.second.read.seq.toString();

	//Trim head and tail of both ends containing errors
	std::string seqFirst = trimRead(seqFirstOriginal, m_params.kmerLength, threshold,m_params.indices);
	std::string seqSecond = trimRead(seqSecondOriginal, m_params.kmerLength, threshold,m_params.indices);

	if(seqFirst.length()<(size_t)m_params.minOverlap || seqSecond.length()<(size_t)m_params.minOverlap)
	return result;
	
	// if(isSuitableForFMWalk(seqFirst, seqSecond))
	// {
	//extract prefix of seqFirst
	std::string firstKRstr = seqFirst.substr(0, m_params.minOverlap);	
	//extract suffix of seqSecond
	std::string secondKRstr = seqSecond.substr(0, m_params.minOverlap);
	
	//Walk from the 1st end to 2nd end
	size_t maxOverlap = m_params.maxOverlap!=-1?m_params.maxOverlap:
	((workItemPair.first.read.seq.length()+workItemPair.second.read.seq.length())/2)*0.9;
	
	SAIntervalTree SAITree(&firstKRstr, m_params.minOverlap, maxOverlap, m_params.maxInsertSize, m_params.maxLeaves,
	m_params.indices, reverseComplement(secondKRstr), threshold);
	std::string mergedseq;
	SAITree.mergeTwoReads(mergedseq);

	//Walk from the 2nd end to 1st end 
	SAIntervalTree SAITree2(&secondKRstr, m_params.minOverlap, maxOverlap, m_params.maxInsertSize, m_params.maxLeaves,
	m_params.indices, reverseComplement(firstKRstr), threshold);
	std::string mergedseq2;
	SAITree2.mergeTwoReads(mergedseq2);
	
	//Unipath from 1st end but no path from 2nd end
	if(!mergedseq.empty() && mergedseq2.empty() )
	{
		result.merge = true ;
		result.correctSequence = mergedseq ;
		return result;
		
		//Unipath from 2nd end but no path from 1st end
	}
	else if(mergedseq.empty() && !mergedseq2.empty() )
	{				
		result.merge = true ;
		result.correctSequence = mergedseq2 ;
		return result;
		
	}
	else if( !mergedseq.empty() && !mergedseq2.empty() && (mergedseq.length()==mergedseq2.length())  )	//mergedseq.length()-mergedseq2.length()<=3
	{
		// if(SAITree.getMaxUsedLeaves()>100){
		// std::cout << ">" << mergedseq.length() << "\n" << mergedseq << "\n";
		// getchar();
		// }
		// if(mergedseq.length()>maxOverlap){
		result.merge = true ;
		result.correctSequence = (SAITree.getKmerCoverage()>SAITree2.getKmerCoverage())? mergedseq:mergedseq2 ;
		return result;
		// }
	}
	// }//end of length > min overlap
	// else
	// std::cout << ">First\n" << seqFirstOriginal << "\n>Second\n" << seqSecondOriginal<< "\n";
	
	return result;
}

//
FMIndexWalkResult FMIndexWalkProcess::KmerizeReads(const SequenceWorkItem& workItem)
{
	FMIndexWalkResult result;

	//get parameters
	size_t kmerLength = m_params.kmerLength ;
	size_t threshold = (size_t)CorrectionThresholds::Instance().getRequiredSupport(0)-1;

	std::string seqFirst  = workItem.read.seq.toString() ;
	if(seqFirst.length()<kmerLength) return result;

	//Trim head and tail from both ends if there is low-frequency kmer
	// seqFirst = trimRead(seqFirst, m_params.kmerLength, threshold, m_params.indices);
	
	//Compute kmer freq of each kmer
	KmerContext seqFirstKC(seqFirst, kmerLength, m_params.indices);

	std::vector<std::string> firstKR ;
	int firstMainIdx=-1;

	if(seqFirst.length()>=(size_t) kmerLength) 
	firstMainIdx = splitRead( seqFirstKC, firstKR, threshold, m_params.indices);

	/*** write kmernized results***/
	if (!firstKR.empty()) result.kmerize =true ;
	
	for (int i = 0 ; i<(int)firstKR.size() ; i ++ )
	{
		std::string kmerRead = firstKR.at(i);
		// float GCratio =0 ;
		// if ( isLowComplexity (kmerRead,GCratio) ) continue;
		// if ( maxCon(kmerRead)*3 > kmerRead.length() ) continue;
		if (i==firstMainIdx)  result.correctSequence = kmerRead ;
		else
		result.kmerizedReads.push_back(kmerRead);
	}
	
	return result;
}

FMIndexWalkResult FMIndexWalkProcess::ValidateReads(const SequenceWorkItem& workItem)
{
	FMIndexWalkResult result;

	//get parameters
	size_t kmerLength = m_params.kmerLength ;
	size_t threshold = (size_t)CorrectionThresholds::Instance().getRequiredSupport(0)-1;

	std::string seqFirst  = workItem.read.seq.toString() ;
	
	if(seqFirst.length()<=(size_t)m_params.minOverlap)
	{
		float GCratio;
		if(!isLowComplexity(seqFirst, GCratio))
		{
			result.kmerize=true;
			result.correctSequence = seqFirst ;
			return result;
		}
		else
		{
			result.correctSequence = seqFirst ;
			return result;
		}
	}

	// std::cout << ">" << workItem.read.id<< "\n" ;
	//maxOverlap is limited to 90% of read length which aims to prevent over-greedy search
	const size_t maxOverlap = m_params.maxOverlap!=-1?m_params.maxOverlap:seqFirst.length()*0.9;
	const size_t maxSearchDepth = seqFirst.length()*1.1;
	
	std::string mergedseq1, mergedseq2;
	//Walk from the 1st end to 2nd end											
	SAIntervalTree SAITree1(&seqFirst, m_params.minOverlap, maxOverlap, maxSearchDepth, m_params.maxLeaves,
							m_params.indices, threshold);
	int flag1 = SAITree1.validate(mergedseq1);

	//Walk from the 2nd end to 1st end using the other strand
	std::string secondKRstr=reverseComplement(seqFirst);
	SAIntervalTree SAITree2(&secondKRstr, m_params.minOverlap, maxOverlap, maxSearchDepth, m_params.maxLeaves,
							m_params.indices, threshold);

	int flag2 = SAITree2.validate(mergedseq2);

	// std::cout << ">" << flag1<< "\n" << mergedseq1 << "\n>" << flag2<< "\n" << mergedseq2 << "\n";
	// std::cout << SAITree1.getMaxUsedLeaves() << "\t" << SAITree1.isBubbleCollapsed() << "\t" << SAITree2.getMaxUsedLeaves() << "\t" << SAITree2.isBubbleCollapsed()<<"\n";
	// getchar();

	double diff1=(double)mergedseq1.length()/seqFirst.length();
	double diff2=(double)mergedseq2.length()/seqFirst.length();
	
	// if(!mergedseq1.empty() && mergedseq2.empty() && isDiff1Acceptable && flag2!=-2)
	if(!mergedseq1.empty() && mergedseq2.empty() && flag2!=-2)
	{
		// std::cout << "Case 1: " << flag1 << "\t"<< flag2 << "\t"<< diff1 << "\t" << diff2 <<"\n";
		result.merge = true ;
		// the longer seq is often the correct one, possibly due to repeat collapse during FM-index walk
		result.correctSequence = diff1>=1?mergedseq1 : seqFirst;
		
		return result;

	// }else if( !mergedseq2.empty() && mergedseq1.empty() && isDiff2Acceptable && flag1!=-2)
	}
	else if( !mergedseq2.empty() && mergedseq1.empty() && flag1!=-2)
	{
		// std::cout << "Case 2: " << flag1 << "\t"<< flag2 << "\t"<< diff1 << "\t" << diff2 <<"\n";
		result.merge = true ;
		// the longer seq is often the correct one, possibly due to repeat collapse during FM-index walk
		result.correctSequence = diff2>=1?mergedseq2 : seqFirst;
		return result;
	}
	else if( !mergedseq1.empty() && !mergedseq2.empty() )
	{
		result.merge = true ;
		// the longer seq is often the correct one, possibly due to repeat collapse during FM-index walk
		if(diff1>=1)
			result.correctSequence = mergedseq1;
		else if(diff2>=1)
			result.correctSequence = mergedseq2;
		else
			result.correctSequence = seqFirst;
		return result;
	}
	
	/** Case 3: kmerize the remaining reads **/
	assert(seqFirst.length() >= (size_t) kmerLength);

	//Compute kmer freq of each kmer
	// KmerContext seqFirstKC(seqFirst, kmerLength, m_params.indices);
	std::vector<std::string> firstKR ;
	int firstMainIdx=-1;

	// FM walk failed due to error kmers
	// firstMainIdx = splitRead( seqFirstKC, firstKR, threshold, m_params.indices);
	
	// If FM walk failed due to large chimera repeats, the read may not be splitted
	// if(firstMainIdx>=0 && !firstKR.empty() && firstKR.at(firstMainIdx).length() == seqFirst.length())
	// {
		// firstKR.clear();
		// firstMainIdx = splitRepeat( seqFirstKC, firstKR);
	// }	

	firstMainIdx = splitRead( seqFirst, firstKR, threshold, m_params.indices);
	
	/*** write kmernized results***/
	if (!firstKR.empty()) result.kmerize =true ;
	
	for (int i = 0 ; i<(int)firstKR.size() ; i ++ )
	{
		std::string kmerRead = firstKR.at(i);
		float GCratio =0 ;
		if ( isLowComplexity (kmerRead,GCratio) ) continue;
		
		if (i==firstMainIdx)  
			result.correctSequence = kmerRead ;
		else
			result.kmerizedReads.push_back(kmerRead);
	}
	
	return result;
	
}

// PacBio Self Correction by Ya, v20151001.
FMIndexWalkResult FMIndexWalkProcess::PBSelfCorrection(const SequenceWorkItem& workItem)
{	
	FMIndexWalkResult result;
	std::vector<std::pair<int, std::string> > seeds, pacbioCorrectedStrs;
	std::string readSeq = workItem.read.seq.toString();
		
	// find seeds
	seeds = searchingSeedsUsingSolidKmer(readSeq);	
	result.totalSeedNum = seeds.size();
	
	// initialize corrected pacbio reads string
	if(seeds.size() >= 1)
	{
		result.correctedLen += seeds[0].second.length();
		pacbioCorrectedStrs.push_back(seeds[0]);
	}
	else
	{
		result.merge = false;
		return result;
	}
	
	// FMWalk for each pair of seeds
	for(int targetSeed = 1 ; targetSeed < seeds.size() ; targetSeed++)
	{
		int FMWalkReturnType = -1;
		std::string mergedseq;
		std::pair<int,std::string> source = pacbioCorrectedStrs.back();
		
		// A lot of goals
		for(int nextTargetSeed = 0 ; nextTargetSeed < m_params.downward && targetSeed + nextTargetSeed < seeds.size() ; nextTargetSeed++)
		{
			// <distance between seeds, read(ATCG)>
			std::vector<std::pair<int, std::string> > targets;
			
			// Goals are collected to find PB reads
			for(int collectedSeed = 0, currentTarget = targetSeed + nextTargetSeed ; collectedSeed < m_params.collectedSeeds && (currentTarget + collectedSeed) < seeds.size() ; collectedSeed++)
			{
				int dis_between_seeds = seeds[currentTarget+collectedSeed].first - seeds[targetSeed-1].first - seeds[targetSeed-1].second.length();
				std::string target_str = seeds[currentTarget+collectedSeed].second;
				targets.push_back(make_pair(dis_between_seeds, target_str));
			}
			
			std::cout << result.totalWalkNum << "----\n";
			SAIntervalTreeForPBGap SAITree(&source.second, m_params.minKmerLength,
			targets,
			m_params.maxLeaves, m_params.indices.pBWT, m_params.indices.pRBWT,
			m_params.FMWKmerThreshold);
			FMWalkReturnType = SAITree.mergeTwoSeedsUsingHash(mergedseq);
			
			std::cout << pacbioCorrectedStrs.size()-1 << ": " //<< reverseComplement((source.second).substr(source.second.length()-minOverlap)) << " " << reverseComplement((target.second).substr(0, minOverlap)) << " "
				 //<< (source.second).substr(source.second.length()-minOverlap) << " " << (target.second).substr(0, minOverlap) << " "
				 << source.first << "-" << source.first+source.second.length()-1 <<  ":" << source.second.length() << ", " 
				 <<	seeds[targetSeed+nextTargetSeed].first << "-" << seeds[targetSeed+nextTargetSeed].first+seeds[targetSeed+nextTargetSeed].second.length()-1 <<  ":" << seeds[targetSeed+nextTargetSeed].second.length() << ", dis: "
				 << targets[0].first << ". " << FMWalkReturnType << ".\n";
			
			// FMWalk success
			if(FMWalkReturnType > 0)
			{				
				size_t gainPos = source.second.length();
				assert(mergedseq.length() > gainPos);
				std::string gainStr = mergedseq.substr(gainPos);
				pacbioCorrectedStrs.back().second += gainStr;
				result.correctedLen += gainStr.length();
				result.correctedNum++;
				result.seedDis += seeds[targetSeed+nextTargetSeed].first - source.first - source.second.length();
				targetSeed = targetSeed + nextTargetSeed;
				break;
			}
		}
		
		// FMWalk failure: 
		// 1. high error 
		// 2. exceed leaves
		// 3. exceed depth
		if(FMWalkReturnType < 0)
		{
			// seed distance
			result.seedDis += seeds[targetSeed].first - source.first - source.second.length();
			result.correctedLen += seeds[targetSeed].second.length();
			/*if(round != 1)
			{
				// not cut off
				pacbioCorrectedStrs.back().second += readSeq.substr(source.first + source.second.length(), 
																seeds[targetSeed].first + seeds[targetSeed].second.length() - source.first - source.second.length());
			}
			else if(round == 1)*/
			{
				// cut off
				pacbioCorrectedStrs.push_back(seeds[targetSeed]);
			}
			if(FMWalkReturnType == -1)
				result.highErrorNum++;
			else if(FMWalkReturnType == -2)
				result.exceedDepthNum++;
			else if(FMWalkReturnType == -3)
				result.exceedLeaveNum++;
		}
		result.totalWalkNum++;
	}
	
	result.merge = true;
	result.totalReadsLen = readSeq.length();
	for(size_t result_count = 0 ; result_count < pacbioCorrectedStrs.size() ; result_count++)
		result.correctedPacbioStrs.push_back(pacbioCorrectedStrs[result_count].second);
	return result;
}

std::vector<std::pair<int, std::string> > FMIndexWalkProcess::searchingSeedsUsingSolidKmer(const std::string readSeq)
{
	std::vector<std::pair<int, std::string> > seeds;
	int kmerLen = m_params.kmerLength, 
		kmerThreshold = m_params.seedKmerThreshold, 
		readLen = readSeq.length();

	if(readLen >= kmerLen)
	{
		for(int i = 0 ; i+kmerLen <= readLen ; i++)
		{
			std::string kmer = readSeq.substr(i, kmerLen);
			int kmerFreqs = 0;
			kmerFreqs += BWTAlgorithms::countSequenceOccurrencesSingleStrand(kmer, m_params.indices);
			kmerFreqs += BWTAlgorithms::countSequenceOccurrencesSingleStrand(reverseComplement(kmer), m_params.indices);
			
			if(kmerFreqs >= kmerThreshold)
			{
				int seedStartPos = i, 
					seedLen = 0;
				
				for(i++ ; i+kmerLen <= readLen ; i++)
				{
					kmer = readSeq.substr(i, kmerLen);
					kmerFreqs = 0;
					kmerFreqs += BWTAlgorithms::countSequenceOccurrencesSingleStrand(kmer, m_params.indices);
					kmerFreqs += BWTAlgorithms::countSequenceOccurrencesSingleStrand(reverseComplement(kmer), m_params.indices);
					
					if(kmerFreqs >= kmerThreshold)
						seedLen++;
					else
						break;
				}
				
				seeds.push_back(make_pair(seedStartPos, readSeq.substr(seedStartPos, seedLen+kmerLen)));
				i = i - 2 + kmerLen;
			}
		}
	}
	
	return seeds;
}

// PacBio Hybrid Correction by Ya, v20150617.
FMIndexWalkResult FMIndexWalkProcess::PBHybridCorrection(const SequenceWorkItem& workItem)
{
	FMIndexWalkResult result;
	
	// get parameters
	string readSeq = workItem.read.seq.toString();
	string corReadSeq = readSeq;
	vector<string> pacbioCorrectedStrs;	
	vector<pair<int,string> > seeds;
	
	for(int round = 3 ; round > 0 ; round--)
	{
		// initialize
		result.correctedLen = 0;
		pacbioCorrectedStrs.clear();
		seeds.clear();
		
		// find seeds
		seeds = findSeedsUsingDynamicKmerLen(corReadSeq);
		
		// initialize corrected pacbio reads string
		if(seeds.size() >= 1)
		{
			result.correctedLen += seeds[0].second.length();
			pacbioCorrectedStrs.push_back(seeds[0].second);
		}
		else
		{
			result.merge = false;
			return result;
		}
		
		// FMWalk for each pair of seeds
		for(size_t i = 1 ; i < seeds.size() ; i++)
		{			
			int needWalkLen = (seeds[i].first - seeds[i-1].first - seeds[i-1].second.length());
			int FMWalkReturnType = -1, minOverlap;
			string mergedseq;
			pair<int,string> source = seeds[i-1];
			pair<int,string> target = seeds[i];
			
			if(source.second.length() >= m_params.maxOverlap && target.second.length() >= m_params.maxOverlap)
				minOverlap = m_params.maxOverlap-2;
			else
			{
				if(source.second.length() <= target.second.length())
					minOverlap = source.second.length();
				else
					minOverlap = target.second.length();
			}
			
			FMWalkReturnType = solveHighError(source, target, minOverlap, needWalkLen, &mergedseq);
				
			// record corrected pacbio reads string
			// FMWalk success
			if(FMWalkReturnType == 1)
			{
				int gainPos = source.second.length();
				// have gain ground
				if(mergedseq.length() > gainPos)
				{
					string gainStr = mergedseq.substr(gainPos);
					pacbioCorrectedStrs.back() += gainStr;
					if(round == 1)
						result.correctedLen += gainStr.length();
				}
			}
			// FMWalk failure: 
			// 1. high error 
			// 2. exceed leaves
			// 3. exceed depth
			else
			{
				if(round != 1)
				{
					// not cut off
					int startPos = seeds[i-1].first + seeds[i-1].second.length();
					int distanceBetweenSeeds = seeds[i].first + seeds[i].second.length() - seeds[i-1].first - seeds[i-1].second.length();
					pacbioCorrectedStrs.back() += corReadSeq.substr(startPos, distanceBetweenSeeds);
				}
				else if(round == 1)
				{
					// cut off
					pacbioCorrectedStrs.push_back(seeds[i].second);
					result.correctedLen += seeds[i].second.length();
				}
			}
			// output information
			if(round == 3)
			{
				result.totalSeedNum = seeds.size();
				result.totalWalkNum++;				
				if(FMWalkReturnType == 1)
					result.correctedNum++;
				else if(FMWalkReturnType == -1)
					result.highErrorNum++;
				else if(FMWalkReturnType == -2)
					result.exceedDepthNum++;
				else if(FMWalkReturnType == -3)
					result.exceedLeaveNum++;
			}
		}
		
		assert(pacbioCorrectedStrs.size() != 0);
		corReadSeq = pacbioCorrectedStrs.back();
	}
	result.merge = true;
	result.totalReadsLen = readSeq.length();
	for(size_t result_count = 0 ; result_count < pacbioCorrectedStrs.size() ; result_count++)
		result.correctedPacbioStrs.push_back(pacbioCorrectedStrs[result_count]);
	return result;
}

vector<pair<int,string> > FMIndexWalkProcess::findSeedsUsingDynamicKmerLen(const string readSeq)
{
	vector<pair<int,string> > seeds;
	int kmerLen, iniKmerLen, minKmerLen, kmerThreshold;
	
	kmerLen = iniKmerLen = m_params.kmerLength;
	minKmerLen = m_params.minKmerLength;
	kmerThreshold = m_params.seedKmerThreshold;
	
	int readLen = readSeq.length();
	if(readLen >= iniKmerLen)
	{
		bool start = false;
		int newStartPos = -1, newStartPos2 = -1, seedStartPos = 0, walkDistance = 0;
		
		// starting search seeds
		for(int i = 0 ; i+kmerLen <= readLen ; i++)
		{
			string kmer = readSeq.substr(i, kmerLen);
			
			int kmerFreqs = 0;
			kmerFreqs += BWTAlgorithms::countSequenceOccurrencesSingleStrand(kmer, m_params.indices);
			kmerFreqs += BWTAlgorithms::countSequenceOccurrencesSingleStrand(reverseComplement(kmer), m_params.indices);
			
			walkDistance++;
			if(kmerFreqs >= kmerThreshold)	// find the seed.
			{
				// debug
				//cout << round << "-" << kmerLen << ": " << i << ", " << kmerFreqs << ", ";
				//cout << endl << kmer << endl;
				
				start = true;
				seedStartPos = i;
				
				// Until not contiguous.
				for(i++ ; i+kmerLen <= readLen ; i++)
				{
					kmer = readSeq.substr(i, kmerLen);
					kmerFreqs = 0;
					kmerFreqs += BWTAlgorithms::countSequenceOccurrencesSingleStrand(kmer, m_params.indices);
					kmerFreqs += BWTAlgorithms::countSequenceOccurrencesSingleStrand(reverseComplement(kmer), m_params.indices);
					if(kmerFreqs < kmerThreshold)
						break;
				}
				// debug
				//cout << i << ", " << kmerFreqs << ".\n";
				
				seeds.push_back(make_pair(seedStartPos, readSeq.substr(seedStartPos, kmerLen + i - seedStartPos - 1)));
				
				kmerLen = iniKmerLen;
				i = seeds.back().first + seeds.back().second.length() - 1;
				newStartPos2 = i;
				walkDistance = 0;
			}
			else if(walkDistance >= m_params.seedWalkDistance[kmerLen])	// It's too long to walk, so using dynamic kmer.
			{
				walkDistance = 0;
				if(kmerLen <= minKmerLen) // It has not searched the seed using dynamic kmer.
				{
					newStartPos = i;
					newStartPos2 = i;
					kmerLen = iniKmerLen;
				}
				else
				{
					kmerLen -= 2;
					if(start == false)	// It's too long to walk from starting point.
						i = newStartPos;
					else
						i = newStartPos2;
				}
			}
		}
	}
	
	return seeds;
}

int FMIndexWalkProcess::doubleFMWalkForPacbio(pair<int,string> firstSeed, pair<int,string> secondSeed, int minOverlap, int needWalkLen, string* mergedseq)
{
	assert(minOverlap <= firstSeed.second.length() && minOverlap <= secondSeed.second.length());

	int FMWalkReturnType;
	
	SAIntervalPBHybridCTree SAITree(&firstSeed.second, minOverlap, m_params.maxOverlap, needWalkLen, m_params.maxLeaves,
							m_params.indices.pBWT, m_params.indices.pRBWT, secondSeed.second, m_params.FMWKmerThreshold);
	FMWalkReturnType = SAITree.mergeTwoReads(*mergedseq);
	
	if(FMWalkReturnType < 0)
		return FMWalkReturnType;

	assert((*mergedseq).empty() != true);
	
	string mergedseq2;
	string firstSeq = reverseComplement(firstSeed.second);
	string secondSeq = reverseComplement(secondSeed.second);
	SAIntervalPBHybridCTree SAITree2(&secondSeq, minOverlap, m_params.maxOverlap, needWalkLen, m_params.maxLeaves,
							m_params.indices.pBWT, m_params.indices.pRBWT, firstSeq, m_params.FMWKmerThreshold);
	FMWalkReturnType = SAITree2.mergeTwoReads(mergedseq2);

	if((*mergedseq).length() == mergedseq2.length())
		return FMWalkReturnType;
	else if(FMWalkReturnType > 0)
		return -4;
	else if(FMWalkReturnType < 0)
		return FMWalkReturnType;
	else
		assert(false);
}

int FMIndexWalkProcess::solveHighError(pair<int,string> firstSeed, pair<int,string> secondSeed, int minOverlap, int needWalkLen, string* mergedseq)
{
	int FMWalkReturnType;
	int minOverlapTmp = minOverlap;
	
	do
	{
		FMWalkReturnType = doubleFMWalkForPacbio(firstSeed, secondSeed, minOverlapTmp, needWalkLen, mergedseq);
		//minOverlapTmp--;
		//minOverlapTmp-=2;
		minOverlapTmp=(minOverlapTmp*2)/3;
		
	}while(FMWalkReturnType != 1 && minOverlapTmp >= m_params.minKmerLength);
	
	return FMWalkReturnType;
}

//check necessary conditions for FM-index walk
bool FMIndexWalkProcess::isSuitableForFMWalk(std::string& seqFirst, std::string& seqSecond)
{
	//check minimum read length
	bool isBothReadLengthSufficient = seqFirst.length()>= (size_t) m_params.minOverlap && seqSecond.length() >= (size_t)m_params.minOverlap ;
	if(!isBothReadLengthSufficient) return false;

	//estimate repeat kmer
	// const size_t RepeatKmerFreq = m_params.kd.getRepeatKmerCutoff();
	const size_t RepeatKmerFreq = m_params.kd.getMedian()*1.3; 
	size_t KmerFreq1 = BWTAlgorithms::countSequenceOccurrences( seqFirst, m_params.indices.pBWT );
	bool isFirstReadUnique =  KmerFreq1 < RepeatKmerFreq;

	size_t KmerFreq2 = BWTAlgorithms::countSequenceOccurrences( seqSecond, m_params.indices.pBWT );
	bool isSecondReadUnique =  KmerFreq2 < RepeatKmerFreq;
	
	if( (isFirstReadUnique && isSecondReadUnique) ) return true;
	
	//
	// if(KmerFreq1 < m_params.kd.getMedian()*1.8 && KmerFreq2 < m_params.kd.getMedian()*1.8) return 2;
	
	return false;
}

// return complexity of seq, default: 0.9
bool  FMIndexWalkProcess::isLowComplexity (std::string seq , float & GCratio)
{
	size_t seqLen = seq.length();
	size_t countG =0 ;
	size_t countC =0 ;
	size_t countT =0 ;
	size_t countA =0 ;

	for (size_t i=0; i<seqLen; i++)
	{
		switch(seq[i]){
		case 'A': countA ++ ;break;
		case 'T': countT ++ ;break;
		case 'C': countC ++ ;break;
		case 'G': countG ++ ;break;
		default:  assert(false);
		}
	}

	GCratio = (float)(countG+countC)/seqLen ;

	if (  ((float) countA/seqLen >=0.9 ) || ((float) countT/seqLen >=0.9 )
			|| ((float) countC/seqLen >=0.9 ) || ((float) countG/seqLen >=0.9 ) )
	return true;

	return false;

}

//compute the maximum length of consecutive letter AATTTTTTTTTTTCCC
size_t FMIndexWalkProcess::maxCon (std::string s)
{
	size_t c = 1 ;
	size_t max =1 ;
	for (size_t i =1 ;i<s.length();i++)
	{
		if (s[i] =='N') continue;

		if (s[i]!=s[i-1])
		{
			if (c>max) max = c ;
			c=1;
		}
		else
		{
			c++;
			if ( i == s.length() -1 )
			{
				if (c>max) max = c ;
			}
			else if ( s[i]!=s[i+1])
			{
				if (c>max) max = c ;
			}
		}
	}

	return max;

}

//search for a strong interval having high-frequent kmers at both strands 
bool FMIndexWalkProcess::isIntervalExistStrongKmer (std::pair<size_t,size_t> interval,std::vector<size_t> & countQualified)
{
	for(size_t i = interval.first ; i<=interval.second ; i++ )
	{
		//find a high-frequent kmer 
		if (countQualified.at(i)== 2 ) return true;
	}
	return false;
}

//Determine reliability between two intervals defined by existence of strong kmer at one strand
bool FMIndexWalkProcess::isPathReliable(std::pair<size_t,size_t> intervalX, std::pair<size_t,size_t> intervalY,std::vector<size_t> & countQualified)
{
	//Two adjacent strong intervals are assumed to be reliable
	if (intervalX.second+1==intervalY.first) return true;

	size_t start = intervalX.second + 1 ;
	size_t end = intervalY.first-1;
	assert (start<=end);

	//if the path exists strong kmer at one strand, it is reliable.
	//Otherwise, 
	for (size_t i =start  ; i <=end ;i++)
	if (countQualified[i]==0) return false;

	return true;
}

//Merge back two splitted intervals if there exists strong kmer link at one strand
bool FMIndexWalkProcess::isIntervalMerge (std::vector< std::pair<size_t,size_t> > & intervals , std::vector<size_t> & countQualified )
{
	std::vector<bool> stongInterval(intervals.size());
	size_t count = 0 ;
	for (size_t i =0 ;i <intervals.size();i++)
	{
		stongInterval.at(i)= isIntervalExistStrongKmer (intervals[i],countQualified);
		if (stongInterval[i]) count ++ ;
	}
	if (count <2 ) return false;
	else	//This read exists two strong kmers in two intervals, may be over splitted
	{
		int s = -1 ;
		int e = -1 ;
		for (int i=0 ;i<(int)intervals.size();i++)
		{
			//Find the first strong interval
			if (stongInterval.at(i)&& s<0)
			{
				s=i;
				continue;
			}

			//Find the second strong interval
			if (stongInterval[i])
			{
				e=i;
				//check if there exist strong kmers at one strand between them
				if (isPathReliable(intervals[s],intervals[e],countQualified))
				{
					//Extend 1st interval end to 2nd end
					intervals[s].second = intervals[e].second;
					//Erase the intervals after 1st interval (s+1) till 2nd interval e
					intervals.erase(intervals.begin()+s+1,intervals.begin()+e+1);
					return true;
				}
				s=e;
			}
		}
	}
	return false;

}


// Split the read into subreads at potential error bases
int FMIndexWalkProcess::splitRead (KmerContext& seq, std::vector<std::string> & kmerReads, size_t threshold, BWTIndexSet & index)
{
	if (seq.empty()) return -1 ;

	std::vector<size_t> countQualified (seq.numKmer,0) ;
	for (size_t i=0 ;i<seq.numKmer;i++)
	{
		// std::cout << i << ": " <<seq.kmerFreqs_same.at(i) << "\t" << seq.kmerFreqs_revc.at(i) << "\n";
		if (seq.kmerFreqs_same.at(i)>= threshold) countQualified[i]++;
		if (seq.kmerFreqs_revc.at(i)>= threshold) countQualified[i]++;
	}
	

	//Split the reads into intervals
	std::vector< std::pair<size_t,size_t> > intervals ;
	size_t start = 0 ;
	size_t end = seq.numKmer-1 ;
	for (size_t p = 1; p< seq.numKmer ; p++)
	{
		//don't split if both strands have kmer feq >= threshold
		if (countQualified[p-1]==2 && countQualified[p]==2) continue;
		
		//kmerize read at pos p if the path is not simple
		if ( !isSimple(seq.kmers[p-1], seq.kmers[p], index, 1) )
		{
			intervals.push_back(std::make_pair (start,p-1));
			start =p;
		}
	}
		
	intervals.push_back(std::make_pair (start,end));
	
	//merge two strong intervals with one strong kmer link in between
	// while (isIntervalMerge(intervals,countQualified));

	size_t maxIntervalNum = 0;
	int mainSeedIdx = -1 ;
	for (size_t i=0;i<intervals.size();i++)
	{
		if (isIntervalExistStrongKmer(intervals[i],countQualified))
		{
			size_t intervalNum = intervals[i].second - intervals[i].first ;
			if (maxIntervalNum < intervalNum)
			{
				maxIntervalNum = intervalNum;
				mainSeedIdx = i;
			}
		}

		std::string k=seq.readSeq.substr(intervals[i].first, intervals[i].second-intervals[i].first+seq.kmerLength);
		kmerReads.push_back(k);
	}
	
	return mainSeedIdx;
}

// Faster kmerize algorithm using BWT Intervals
// Kmerize the read into subreads at potential error bases
int FMIndexWalkProcess::splitRead (std::string& seq, std::vector<std::string> & kmerReads, size_t threshold, BWTIndexSet & index)
{
	if ((int)seq.length()<m_params.kmerLength) return -1 ;

	std::vector<size_t> countQualified (seq.length()-m_params.kmerLength+1,0);
	std::string startingKmer=seq.substr(0,m_params.kmerLength);
	BWTInterval fwdInterval = BWTAlgorithms::findInterval(index.pRBWT, reverse(startingKmer));
	BWTInterval rvcInterval = BWTAlgorithms::findInterval(index.pBWT, reverseComplement(startingKmer));
	
	size_t currKmerSize=m_params.kmerLength;
	size_t currKmerFreq=0;
	currKmerFreq += fwdInterval.isValid()?fwdInterval.size():0;
	currKmerFreq += rvcInterval.isValid()?rvcInterval.size():0;

	for (size_t i=0 ;i<=seq.length()-m_params.kmerLength;i++)
	{
		if(currKmerFreq >= threshold)
		{
			countQualified[i]++;
			
			if(i<seq.length()-m_params.kmerLength)
			{
				char b = seq.at(i+m_params.kmerLength);
				if(fwdInterval.isValid())
					BWTAlgorithms::updateInterval(fwdInterval, b, index.pRBWT);
					
				if(rvcInterval.isValid())
					BWTAlgorithms::updateInterval(rvcInterval, complement(b), index.pBWT);

				currKmerSize++;
				currKmerFreq = 0;
				currKmerFreq += fwdInterval.isValid()?fwdInterval.size():0;
				currKmerFreq += rvcInterval.isValid()?rvcInterval.size():0;
			}
			continue;
		}
		// unsatisfied due to too large kmer
		else if((int)currKmerSize > m_params.kmerLength && currKmerFreq<threshold) 
		{
			// Recompute BWTInterval
			std::string newKmer = seq.substr(i, m_params.kmerLength);
			fwdInterval = BWTAlgorithms::findInterval(index.pRBWT, reverse(newKmer));
			rvcInterval = BWTAlgorithms::findInterval(index.pBWT, reverseComplement(newKmer));

			currKmerSize = m_params.kmerLength;
			currKmerFreq = 0;
			currKmerFreq += fwdInterval.isValid()?fwdInterval.size():0;
			currKmerFreq += rvcInterval.isValid()?rvcInterval.size():0;
			
			// Rerun this iteration using the new kmer
			i--;
			continue;
		}
		// Bad kmer at i, continue to i++
		else if((int)currKmerSize == m_params.kmerLength && currKmerFreq<threshold)
		{
			if(i<seq.length()-m_params.kmerLength) 
			{
				// Recompute BWTInterval for next i
				std::string newKmer = seq.substr(i+1, m_params.kmerLength);
				fwdInterval = BWTAlgorithms::findInterval(index.pRBWT, reverse(newKmer));
				rvcInterval = BWTAlgorithms::findInterval(index.pBWT, reverseComplement(newKmer));

				currKmerSize = m_params.kmerLength;
				currKmerFreq = 0;
				currKmerFreq += fwdInterval.isValid()?fwdInterval.size():0;
				currKmerFreq += rvcInterval.isValid()?rvcInterval.size():0;			
			}
		}
		else
			assert(false);
	}
	

	//Split the reads into intervals
	std::vector< std::pair<size_t,size_t> > intervals ;
	size_t start = 0 ;
	size_t end = seq.length()-m_params.kmerLength;
	for (size_t p = 1; p < seq.length()-m_params.kmerLength+1 ; p++)
	{
		//don't split if both strands have kmer feq >= threshold
		if (countQualified[p-1]==1 && countQualified[p]==1) continue;
		
		//kmerize read at pos p if the path is not simple
		if ( !isSimple(seq.substr(p-1, m_params.kmerLength), seq.substr(p, m_params.kmerLength), index, 1) )
		{
			intervals.push_back(std::make_pair (start,p-1));
			start =p;
		}
	}
		
	intervals.push_back(std::make_pair (start,end));
	
	size_t maxIntervalSize = 0;
	int mainSeedIdx = -1 ;
	for (size_t i=0;i<intervals.size();i++)
	{
		size_t intervalSize = intervals[i].second - intervals[i].first ;
		if (maxIntervalSize < intervalSize)
		{
			maxIntervalSize = intervalSize;
			mainSeedIdx = i;
		}

		std::string k=seq.substr(intervals[i].first, intervals[i].second-intervals[i].first+m_params.kmerLength);
		kmerReads.push_back(k);
	}
	
	return mainSeedIdx;
}


//Kmerize the read into subreads at potential error bases
int FMIndexWalkProcess::splitRepeat (KmerContext& seq, std::vector<std::string> & kmerReads)
{
	if (seq.empty()) return -1 ;
	//getMedian returns sum of forward and reverse kmers
	const size_t RepeatKmerFreq = m_params.kd.getMedian(); 

	std::vector<size_t> countQualified (seq.numKmer,0) ;
	for (size_t i=0 ;i<seq.numKmer;i++)
	{
		// std::cout << i << ": " <<seq.kmerFreqs_same.at(i) << "\t" << seq.kmerFreqs_revc.at(i) << "\n";
		if (seq.kmerFreqs_same.at(i)> RepeatKmerFreq) countQualified[i]++;
		if (seq.kmerFreqs_revc.at(i)> RepeatKmerFreq) countQualified[i]++;
	}
	

	//Split the reads into intervals
	std::vector< std::pair<size_t,size_t> > intervals ;
	size_t start = 0 ;
	size_t end = seq.numKmer-1 ;
	for (size_t p = 1; p< seq.numKmer ; p++)
	{
		// split if p-1 is not repeat and p is repeat or vice versa
		if ( (countQualified[p-1]<2 && countQualified[p]==2) 
			|| (countQualified[p-1]==2 && countQualified[p]<2) ) 
		{
			//kmerize read at pos p if the path is not simple
			intervals.push_back(std::make_pair (start,p-1));
			start =p;
		}
	}
		
	intervals.push_back(std::make_pair (start,end));
	

	size_t maxIntervalNum = 0;
	int mainSeedIdx = -1 ;
	for (size_t i=0;i<intervals.size();i++)
	{
		if (isIntervalExistStrongKmer(intervals[i],countQualified))
		{
			size_t intervalNum = intervals[i].second - intervals[i].first ;
			if (maxIntervalNum < intervalNum)
			{
				maxIntervalNum = intervalNum;
				mainSeedIdx = i;
			}
		}

		std::string k=seq.readSeq.substr(intervals[i].first, intervals[i].second-intervals[i].first+seq.kmerLength);
		kmerReads.push_back(k);
	}
	
	return mainSeedIdx;
}

// bool FMIndexWalkProcess::existStrongLink (std::string Lkmer,std::string Rkmer,BWTIndexSet & index,size_t threshold)
// {
// assert (Lkmer.substr(1,Lkmer.length()-1) == Rkmer.substr(0,Lkmer.length()-1) );

// size_t LkmerSameFreq = BWTAlgorithms::countSequenceOccurrencesSingleStrand(Lkmer, index);
// size_t LkmerRevcFreq = BWTAlgorithms::countSequenceOccurrencesSingleStrand(reverseComplement(Lkmer), index);

// size_t RkmerSameFreq = BWTAlgorithms::countSequenceOccurrencesSingleStrand(Rkmer, index);
// size_t RkmerRevcFreq = BWTAlgorithms::countSequenceOccurrencesSingleStrand(reverseComplement(Rkmer), index);

// bool LKmerStrong = (LkmerSameFreq>=threshold && LkmerRevcFreq>=threshold) ;
// bool RKmerStrong = (RkmerSameFreq>=threshold && RkmerRevcFreq>=threshold) ;

// assert (  LKmerStrong != RKmerStrong  )  ;


// if ( LKmerStrong )	return  existNextStrongKmer (Lkmer,NK_END,index,threshold) && (numNextKmer(Rkmer,NK_START,index) == 1);
// //RKmerStrong
// else 	return  existNextStrongKmer (Rkmer,NK_START,index,threshold) && (numNextKmer(Lkmer,NK_END,index) == 1 );

// }

//return true or false if there is a strong/better kmer 
bool FMIndexWalkProcess::existNextStrongKmer(std::string kmer , NextKmerDir dir ,BWTIndexSet & index,size_t threshold)
{
	char nBases[4] = {'A','T','C','G'} ;
	int kmerLength = kmer.length() ;

	for (size_t i = 0 ; i < 4 ; i++)
	{
		std::string nextKmer;
		if (dir == NK_START) nextKmer = nBases[i] + kmer.substr(0,kmerLength-1);
		else if  (dir == NK_END) nextKmer = kmer.substr(1,kmerLength-1) + nBases[i];
		else assert(false);

		size_t nextKmerSameFreq = BWTAlgorithms::countSequenceOccurrencesSingleStrand(nextKmer, index);
		size_t nextKmerRevcFreq = BWTAlgorithms::countSequenceOccurrencesSingleStrand(reverseComplement(nextKmer), index);
		if (nextKmerSameFreq>=threshold && nextKmerRevcFreq>=threshold) return true ;

	}
	return false;
}


std::string FMIndexWalkProcess::trimRead( std::string readSeq, size_t kmerLength, size_t /*threshold*/, BWTIndexSet & index)
{
	int head=0, tail=readSeq.length()-kmerLength;
	// if (( kc.kmerFreqs_same[head] < threshold || kc.kmerFreqs_revc [head] < threshold ) && ( numNextKmer(kc.kmers[head],NK_START,index) == 0 ))
	
	//Dead end
	if (numNextKmer( readSeq.substr(head, kmerLength), NK_START, index, 1) == 0  )
	{
		//search for the first pos with >=2 branching kmer path, note that the min kmer threshold should be 1 as error kmer has low freq
		for (head = head+1 ; head <= tail ; head++ )
		if (numNextKmer(readSeq.substr(head,kmerLength), NK_START, index, 1) >= 2) break ;
	}

	// if (( kc.kmerFreqs_same[tail] < threshold || kc.kmerFreqs_revc [tail] < threshold )  && ( numNextKmer(kc.kmers[tail],NK_END,index) == 0 ) )
	if ( numNextKmer( readSeq.substr(tail, kmerLength), NK_END, index, 1) == 0  )
	{
		for (tail = tail-1 ; tail >= head  ; tail-- )
		if (numNextKmer( readSeq.substr(tail, kmerLength), NK_END,index, 1) >= 2) break ;
	}

	//all kemrs are dirty , return empty
	if (head > tail)
	return "";
	else
	return readSeq.substr(head,tail-head+kmerLength);

}



size_t FMIndexWalkProcess::numNextKmer(std::string kmer , NextKmerDir dir ,BWTIndexSet & index, size_t threshold)
{
	size_t num = 0 ;
	char nBases[4] = {'A','T','C','G'} ;
	int kmerLength = kmer.length() ;

	for (size_t i = 0 ; i < 4 ; i++)
	{
		std::string next_mer;
		if (dir == NK_START) next_mer = nBases[i] + kmer.substr(0,kmerLength-1);
		else if  (dir == NK_END) next_mer = kmer.substr(1,kmerLength-1) + nBases[i];

		if ( BWTAlgorithms::countSequenceOccurrences(next_mer,index) >= threshold) num++;
	}
	return num ;
}


bool FMIndexWalkProcess::isSimple (std::string Lkmer, std::string Rkmer, BWTIndexSet & index, size_t threshold)
{
	size_t LKmerPathCount = numNextKmer(Lkmer, NK_END, index, threshold);
	size_t RKmerPathCount = numNextKmer(Rkmer, NK_START, index, threshold);

	if ( LKmerPathCount == 1 &&   RKmerPathCount == 1 )  
		return true;
	else 
		return false;
	
	// if( (LKmerPathCount > 1 && LCount == 0) || (RKmerPathCount>1 && RCount == 0) )
	// return false;
	// else if( LKmerPathCount > 1 && (LCount ==1 && existNextStrongKmer (Lkmer,NK_END, index, threshold)) )
	// return false;
	// else if( RKmerPathCount > 1 && RCount ==1 && existNextStrongKmer (Rkmer,NK_START, index, threshold))
	// return false;
	// else 
	// return true;
}



//
//
//
FMIndexWalkPostProcess::FMIndexWalkPostProcess(std::ostream* pCorrectedWriter,
std::ostream* pDiscardWriter,
const FMIndexWalkParameters params) :
m_pCorrectedWriter(pCorrectedWriter),
m_pDiscardWriter(pDiscardWriter),
m_params(params),
m_kmerizePassed(0),
m_mergePassed(0),
m_qcFail(0),												
m_totalReadsLen(0),
m_correctedLen(0),
m_totalSeedNum(0),
m_totalWalkNum(0),
m_correctedNum(0),
m_highErrorNum(0),
m_exceedDepthNum(0),
m_exceedLeaveNum(0),
m_equalSeedNum(0),
m_seedDis(0)
{
	if(params.algorithm == FMW_VALIDATE)
		m_pLowComplexWriter = createWriter("LowComplexityReads.fa");
}

//
FMIndexWalkPostProcess::~FMIndexWalkPostProcess()
{	
	if(m_params.algorithm == FMW_PACBIOSELF || m_params.algorithm == FMW_PACBIOHYB)
	{
		std::cout << std::endl;
		std::cout << "totalReadsLen: " << m_totalReadsLen << ", ";
		std::cout << "correctedLen: " << m_correctedLen << ", ratio: " << (float)(m_correctedLen)/m_totalReadsLen << "." << std::endl;
		std::cout << "totalSeedNum: " << m_totalSeedNum << "." << std::endl;
		std::cout << "totalWalkNum: " << m_totalWalkNum << ", ";
		std::cout << "correctedNum: " << m_correctedNum << ", ratio: " << (float)(m_correctedNum*100)/m_totalWalkNum << "%." << std::endl;
		std::cout << "highErrorNum: " << m_highErrorNum << ", ratio: " << (float)(m_highErrorNum*100)/m_totalWalkNum << "%." << std::endl;
		std::cout << "exceedDepthNum: " << m_exceedDepthNum << ", ratio: " << (float)(m_exceedDepthNum*100)/m_totalWalkNum << "%." << std::endl;
		std::cout << "exceedLeaveNum: " << m_exceedLeaveNum << ", ratio: " << (float)(m_exceedLeaveNum*100)/m_totalWalkNum << "%." << std::endl;
		std::cout << "equalSeedNum: " << m_equalSeedNum << ", ratio: " << (float)(m_equalSeedNum*100)/m_totalWalkNum << "%." << std::endl;
		std::cout << "disBetweenSeeds: " << m_seedDis/m_totalWalkNum << std::endl << std::endl;
	}
	else
	{
		std::cout << "Reads are kmerized: " << m_kmerizePassed << "\n";
		std::cout << "Reads are merged : "<< m_mergePassed << "\n";
		std::cout << "Reads failed to kmerize or merge: " << m_qcFail << "\n";
	}
}


// Writting results for kmerize and validate
void FMIndexWalkPostProcess::process(const SequenceWorkItem& item, const FMIndexWalkResult& result)
{
	if(m_params.algorithm == FMW_PACBIOSELF || m_params.algorithm == FMW_PACBIOHYB)
	{
		m_totalReadsLen += result.totalReadsLen;
		m_correctedLen += result.correctedLen;
		m_totalSeedNum += result.totalSeedNum;
		m_totalWalkNum += result.totalWalkNum;
		m_correctedNum += result.correctedNum;
		m_highErrorNum += result.highErrorNum;
		m_exceedDepthNum += result.exceedDepthNum;
		m_exceedLeaveNum += result.exceedLeaveNum;
		m_equalSeedNum += result.equalSeedNum;
		m_seedDis += result.seedDis;
		
		if (result.merge)
		{
			//cout << result.correctSequence.toString();
			/*SeqItem mergeRecord;
			stringstream ss;
			ss << item.read.id << "_before_len:" << result.correctSequence.toString().length();
			mergeRecord.id = ss.str();
			mergeRecord.seq = result.correctSequence;
			mergeRecord.write(*m_pCorrectedWriter);*/
			
			for(size_t i = 0 ; i < result.correctedPacbioStrs.size() ; i++)
			{
				SeqItem mergeRecord2;
				std::stringstream ss2;
				ss2 << item.read.id << "_" << i << "_" << result.correctedPacbioStrs[i].toString().length();
				mergeRecord2.id = ss2.str();
				mergeRecord2.seq = result.correctedPacbioStrs[i];
				mergeRecord2.write(*m_pCorrectedWriter);
			}
		}
	}
	else
	{
		// Determine if the read should be discarded
		bool readQCPass = result.kmerize || result.merge;
		if (result.kmerize)
			m_kmerizePassed += 1;
		else if (result.merge)
			m_mergePassed += 1;
		else
		{
			readQCPass = false;
			m_qcFail += 1;
		}

		SeqRecord record = item.read;
		record.seq = result.correctSequence;

		// for validate algorithm
		if (result.merge)
		{
			SeqItem mergeRecord ;
			mergeRecord.id = record.id;
			mergeRecord.seq = result.correctSequence;
			mergeRecord.write(*m_pCorrectedWriter);
		}
		// for validate and kmerize algorithms
		else if (result.kmerize)
		{
			// assert(!result.correctSequence.empty() || !result.kmerizedReads.empty());
			if (!result.correctSequence.empty())
				record.write(*m_pDiscardWriter);
		
			for (size_t i=0 ; i< result.kmerizedReads.size() ; i++)
			{
				record.seq = result.kmerizedReads[i];
				record.writeFasta(*m_pDiscardWriter,i);
			}
		}
		else if  (readQCPass || m_pDiscardWriter == NULL)
		{
			record.write(*m_pCorrectedWriter);
		}
		else
		{
			record.write(*m_pLowComplexWriter);
		}
	}
}

// Writting results of FMW_HYBRID and FMW_MERGE
void FMIndexWalkPostProcess::process(const SequenceWorkItemPair& itemPair, const FMIndexWalkResult& result)
{
	if (result.merge)
		m_mergePassed += 1;
	else if (  (m_params.algorithm == FMW_HYBRID)  && (result.kmerize ||  result.kmerize2) )
	{
		if (result.kmerize) m_kmerizePassed += 1;
		else m_qcFail += 1;
		if (result.kmerize2) m_kmerizePassed += 1;
		else m_qcFail += 1;
	}
	else
	m_qcFail += 2;

	SeqRecord firstRecord  = itemPair.first.read;
	SeqRecord secondRecord  = itemPair.second.read;

	if (result.merge)
	{
		SeqItem mergeRecord ;
		mergeRecord.id = firstRecord.id.substr (0, firstRecord.id.find('/') ) ;
		mergeRecord.seq = result.correctSequence;
		mergeRecord.write(*m_pCorrectedWriter);
	}
	else if(m_params.algorithm == FMW_HYBRID )
	{
		if (!result.correctSequence.empty())
		{
			firstRecord.seq = result.correctSequence;
			firstRecord.write(*m_pDiscardWriter);
		}
		for (size_t i=0 ; i< result.kmerizedReads.size() ; i++)
		{
			firstRecord.seq = result.kmerizedReads[i];
			firstRecord.writeFasta(*m_pDiscardWriter,i);
		}

		if (!result.correctSequence2.empty())
		{
			secondRecord.seq = result.correctSequence2;
			secondRecord.write(*m_pDiscardWriter);
		}
		for (size_t i=0 ; i< result.kmerizedReads2.size() ; i++)
		{
			secondRecord.seq = result.kmerizedReads2[i];
			secondRecord.writeFasta(*m_pDiscardWriter,i);
		}
	}
}
