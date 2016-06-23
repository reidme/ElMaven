#include "PeakDetector.h"

PeakDetector::PeakDetector() {
}
PeakDetector::PeakDetector(MavenParameters* mp) {
	mavenParameters = mp;
}

vector<EIC*> PeakDetector::pullEICs(mzSlice* slice,
		std::vector<mzSample*>&samples, int peakDetect, int smoothingWindow,
		int smoothingAlgorithm, float amuQ1, float amuQ3,
		int baseline_smoothingWindow, int baseline_dropTopX) {

	vector<EIC*> eics;
	vector<mzSample*> vsamples;

	//considering only the selected samples into vsamples
	for (unsigned int i = 0; i < samples.size(); i++) {
		if (samples[i] == NULL)
			continue;
		if (samples[i]->isSelected == false)
			continue;
		vsamples.push_back(samples[i]);
	}

	if (vsamples.size()) {
		/*EicLoader::PeakDetectionFlag pdetect = (EicLoader::PeakDetectionFlag) peakDetect;
		 QFuture<EIC*>future = QtConcurrent::mapped(vsamples, EicLoader(slice, pdetect,smoothingWindow, smoothingAlgorithm, amuQ1,amuQ3));

		 //wait for async operations to finish
		 future.waitForFinished();

		 QFutureIterator<EIC*> itr(future);

		 while(itr.hasNext()) {
		 EIC* eic = itr.next();
		 if ( eic && eic->size() > 0) eics.push_back(eic);
		 }
		 */

		/*
		 QList<EIC*> _eics = result.results();
		 for(int i=0; i < _eics.size(); i++ )	{
		 if ( _eics[i] && _eics[i]->size() > 0) {
		 eics.push_back(_eics[i]);
		 }
		 }*/
	}

	// single threaded version - getting EICs of selected samples.
	for (unsigned int i = 0; i < vsamples.size(); i++) {
		mzSample* sample = vsamples[i]; //sample #
		Compound* c = slice->compound; //compound in slice

		EIC* e = NULL; //init eic

		if (!slice->srmId.empty()) {
			//if srmId of the slice is present, get EICs on the basis of srmId
			cout << "computeEIC srm:" << slice->srmId << endl;
			e = sample->getEIC(slice->srmId);
		} else if (c && c->precursorMz > 0 && c->productMz > 0) {
			//if product and parent ion's m/z of the compound in slice is present, get EICs for QQQ mode
			cout << "computeEIC qqq: " << c->precursorMz << "->" << c->productMz
					<< endl;
			e = sample->getEIC(c->precursorMz, c->collisionEnergy, c->productMz,
					amuQ1, amuQ3);
		} else {
			//else, get EICs for with a m/z and rt window
			/*
			 cout << "computeEIC mzrange" << setprecision(7) << slice->mzmin
			 << " " << slice->mzmax << slice->rtmin << " "
			 << slice->rtmax << endl;
			 */
			e = sample->getEIC(slice->mzmin, slice->mzmax, slice->rtmin,
					slice->rtmax, 1);
		}

		if (e) {
			//if eic exists, perform smoothing
			EIC::SmootherType smootherType =
					(EIC::SmootherType) smoothingAlgorithm;
			e->setSmootherType(smootherType);
			e->setBaselineSmoothingWindow(baseline_smoothingWindow);
			e->setBaselineDropTopX(baseline_dropTopX);
			e->getPeakPositions(smoothingWindow);
			//smoohing over

			//push eic to all eics vector
			eics.push_back(e);
		}
	}
	return eics;
}

void PeakDetector::processSlices() {
	processSlices(mavenParameters->_slices, "sliceset");
}

void PeakDetector::processSlice(mzSlice& slice) {
	vector<mzSlice*> slices;
	slices.push_back(&slice); //add slice to slices vector
	processSlices(slices, "sliceset");
}

void PeakDetector::processMassSlices() {

	//init
	mavenParameters->showProgressFlag = true;
	mavenParameters->checkConvergance = true;
	QTime timer;
	timer.start();

	mavenParameters->setAverageScanTime(); //find avgScanTime

	MassSlices massSlices;
	massSlices.setSamples(mavenParameters->samples);
	massSlices.algorithmB(mavenParameters->ppmMerge,
			mavenParameters->minGroupIntensity, mavenParameters->rtStepSize); //perform algorithmB for samples

	if (massSlices.slices.size() == 0)
		massSlices.algorithmA(); //if no slices present, perform algorithmA TODO WHY?!

	//sort the massslices based on their intensities to enurmerate good slices.
	sort(massSlices.slices.begin(), massSlices.slices.end(),
			mzSlice::compIntensity);

	if (massSlices.slices.size() == 0) {
		//	emit (updateProgressBar("Quiting! No good mass slices found", 1, 1)); TODO: Fix emit.
		return;
	}

	//process goodslices
	processSlices(massSlices.slices, "allslices");

	//cleanup
	delete_all(massSlices.slices);

	qDebug() << "processMassSlices() Done. ElepsTime=%1 msec"
			<< timer.elapsed();
}

vector<mzSlice*> PeakDetector::processCompounds(vector<Compound*> set,
		string setName) {

	mavenParameters->limitGroupCount = INT_MAX; //must not be active when processing compounds

	//iterating over all compounds in the set
	vector<mzSlice*> slices;
	for (unsigned int i = 0; i < set.size(); i++) {
		Compound* c = set[i];
		if (c == NULL)
			continue;

		mzSlice* slice = new mzSlice();
		slice->compound = c; //set slice.compound as compound in the set
		if (!c->srmId.empty()) // with SRM id, update slice.srmId
			slice->srmId = c->srmId;

		if (!c->formula.empty()) { //with formula, calculate mass here
			double mass = mavenParameters->mcalc.computeMass(c->formula,
					mavenParameters->ionizationMode); //compute mass from compound's formula
			slice->mzmin = mass
					- mavenParameters->compoundPPMWindow * mass / 1e6; //find min ppm using compoundPPMWindow
			slice->mzmax = mass
					+ mavenParameters->compoundPPMWindow * mass / 1e6; //find max ppm using compoundPPMWindow
		} else if (c->mass > 0) { //mass already calculated
			double mass = c->mass; //TODO REFACTOR: CAN BE DONE ONLY ONCE
			slice->mzmin = mass
					- mavenParameters->compoundPPMWindow * mass / 1e6; //find min ppm using compoundPPMWindow
			slice->mzmax = mass
					+ mavenParameters->compoundPPMWindow * mass / 1e6; //find max ppm using compoundPPMWindow
		} else {
			continue; //TODO REFACTOR: useless
		}

		if (mavenParameters->matchRtFlag && c->expectedRt > 0) { //if match RT flag is true, and expected RT is > 0
			slice->rtmin = c->expectedRt - mavenParameters->compoundRTWindow; //find min RT using compoundRTWindow
			slice->rtmax = c->expectedRt + mavenParameters->compoundRTWindow;
		} else {
			slice->rtmin = 0; //TODO fixed WHY!?
			slice->rtmax = 1e9;
		}
		slices.push_back(slice);
	}

	return slices;
}

void PeakDetector::pullIsotopes(PeakGroup* parentgroup) {

	// FALSE CONDITIONS
	if (parentgroup == NULL)
		return;
	if (parentgroup->compound == NULL)
		return;
	if (parentgroup->compound->formula.empty() == true)
		return;
	if (mavenParameters->samples.size() == 0)
		return;

	//init
	float ppm = mavenParameters->compoundPPMWindow;
	double maxIsotopeScanDiff = 10;
	double maxNaturalAbundanceErr = 100;
	double minIsotopicCorrelation = 0;
	bool C13Labeled = false;
	bool N15Labeled = false;
	bool S34Labeled = false;
	bool D2Labeled = false;
	int eic_smoothingAlgorithm = 0;

	string formula = parentgroup->compound->formula; //parent formula
	//generate isotope list for parent mass
	vector<Isotope> masslist = mavenParameters->mcalc.computeIsotopes(formula,
			mavenParameters->ionizationMode);

	//iterate over samples to find properties for parent's isotopes.
	map<string, PeakGroup> isotopes;
	map<string, PeakGroup>::iterator itr2;

	for (unsigned int s = 0; s < mavenParameters->samples.size(); s++) {
		mzSample* sample = mavenParameters->samples[s];
		for (int k = 0; k < masslist.size(); k++) {
//			if (stopped())
//				break; TODO: stop
			Isotope& x = masslist[k];
			string isotopeName = x.name;
			double isotopeMass = x.mass;
			double expectedAbundance = x.abundance;

			float mzmin = isotopeMass - isotopeMass / 1e6 * ppm;
			float mzmax = isotopeMass + isotopeMass / 1e6 * ppm;

			float rt = parentgroup->medianRt();
			float rtmin = parentgroup->minRt;
			float rtmax = parentgroup->maxRt;

			Peak* parentPeak = parentgroup->getPeak(sample);
			if (parentPeak)
				rt = parentPeak->rt;
			if (parentPeak)
				rtmin = parentPeak->rtmin;
			if (parentPeak)
				rtmax = parentPeak->rtmax;

			float isotopePeakIntensity = 0;
			float parentPeakIntensity = 0;

			if (parentPeak) {
				parentPeakIntensity = parentPeak->peakIntensity;
				int scannum = parentPeak->getScan()->scannum;
				for (int i = scannum - 3; i < scannum + 3; i++) {
					Scan* s = sample->getScan(i);

					//look for isotopic mass in the same spectrum
					vector<int> matches = s->findMatchingMzs(mzmin, mzmax);

					for (int i = 0; i < matches.size(); i++) {
						int pos = matches[i];
						if (s->intensity[pos] > isotopePeakIntensity) {
							isotopePeakIntensity = s->intensity[pos];
							rt = s->rt;
						}
					}
				}

			}
			//if(isotopePeakIntensity==0) continue;

			//natural abundance check
			if ((x.C13 > 0 && C13Labeled == false) //if isotope is not C13Labeled
			|| (x.N15 > 0 && N15Labeled == false) //if isotope is not N15 Labeled
					|| (x.S34 > 0 && S34Labeled == false) //if isotope is not S34 Labeled
					|| (x.H2 > 0 && D2Labeled == false) //if isotope is not D2 Labeled

					) {
				if (expectedAbundance < 1e-8)
					continue;
				if (expectedAbundance * parentPeakIntensity < 1)
					continue;
				float observedAbundance = isotopePeakIntensity
						/ (parentPeakIntensity + isotopePeakIntensity); //find observedAbundance based on isotopePeakIntensity

				float naturalAbundanceError = abs(
						observedAbundance - expectedAbundance) //if observedAbundance is significant wrt expectedAbundance
				/ expectedAbundance * 100;	// compute natural Abundance Error

				//cerr << isotopeName << endl;
				//cerr << "Expected isotopeAbundance=" << expectedAbundance;
				//cerr << " Observed isotopeAbundance=" << observedAbundance;
				//cerr << " Error="		 << naturalAbundanceError << endl;

				if (naturalAbundanceError > maxNaturalAbundanceErr)
					continue;
			}

			float w = mavenParameters->maxIsotopeScanDiff
					* mavenParameters->avgScanTime;
			double c = sample->correlation(isotopeMass, parentgroup->meanMz,
					ppm, rtmin - w, rtmax + w); //find correlation for isotopes
			if (c < mavenParameters->minIsotopicCorrelation) //if correlation is not less than minimum isotopic correlation STOP
				continue;

			//cerr << "pullIsotopes: " << isotopeMass << " " << rtmin-w << " " <<	rtmin+w << " c=" << c << endl;

			EIC* eic = NULL; //find EIC for isotopes
			for (int i = 0; i < mavenParameters->maxIsotopeScanDiff; i++) {
				float window = i * mavenParameters->avgScanTime;
				eic = sample->getEIC(mzmin, mzmax, rtmin - window,
						rtmax + window, 1);
				//smooth fond eic TODO: null check for found eic
				eic->setSmootherType(
						(EIC::SmootherType) mavenParameters->eic_smoothingAlgorithm);
				eic->getPeakPositions(mavenParameters->eic_smoothingWindow); //find peak position inside eic
				if (eic->peaks.size() == 0)
					continue;
				if (eic->peaks.size() > 1)
					break;
			}
			if (!eic)
				continue;

			//find nearest peak. compute distance between parent peak and all other peaks
			// nearest peak is one with mimimum distance== min RT
			Peak* nearestPeak = NULL;
			float d = FLT_MAX;
			for (int i = 0; i < eic->peaks.size(); i++) {
				Peak& x = eic->peaks[i];
				float dist = abs(x.rt - rt);
				if (dist
						> mavenParameters->maxIsotopeScanDiff
								* mavenParameters->avgScanTime)
					continue;
				if (dist < d) {
					d = dist;
					nearestPeak = &x;
				}
			}

			if (nearestPeak) { //if nearest peak is present
				if (isotopes.count(isotopeName) == 0) { //label the peak of isotope
					PeakGroup g;
					g.meanMz = isotopeMass;
					g.tagString = isotopeName;
					g.expectedAbundance = expectedAbundance;
					g.isotopeC13count = x.C13;
					isotopes[isotopeName] = g;
				}
				isotopes[isotopeName].addPeak(*nearestPeak); //add nearestPeak to isotope peak list
			}
			delete (eic);
		}
	}

	//fill peak group list with the compound and it's isotopes.
	// peak group list would be filled with the parent group, with its isotopes as children
	// click on + to see children == isotopes
	parentgroup->children.clear();
	for (itr2 = isotopes.begin(); itr2 != isotopes.end(); itr2++) {
		string isotopeName = (*itr2).first;
		PeakGroup& child = (*itr2).second;
		child.tagString = isotopeName;
		child.metaGroupId = parentgroup->metaGroupId;
		child.groupId = parentgroup->groupId;
		child.compound = parentgroup->compound;
		child.parent = parentgroup;
		child.setType(PeakGroup::Isotope);
		child.groupStatistics();
		if (mavenParameters->clsf->hasModel()) {
			mavenParameters->clsf->classify(&child);
			child.groupStatistics();
		}
		parentgroup->addChild(child);
		//cerr << " add: " << isotopeName << " " <<	child.peaks.size() << " " << isotopes.size() << endl;
	}
	//cerr << "Done: " << parentgroup->children.size();
	/*
	 //if ((float) isotope.maxIntensity/parentgroup->maxIntensity > 3*ab[isotopeName]/ab["C12 PARENT"]) continue;
	 */
}

void PeakDetector::alignSamples() {
	if (mavenParameters->samples.size() > 1
			&& mavenParameters->alignSamplesFlag) {
		cerr << "Aligning samples" << endl;

		mavenParameters->writeCSVFlag = false;
		processMassSlices();

		cerr << "Aligner=" << mavenParameters->allgroups.size() << endl;
		vector<PeakGroup*> agroups(mavenParameters->allgroups.size());
		for (unsigned int i = 0; i < mavenParameters->allgroups.size(); i++)
			agroups[i] = &mavenParameters->allgroups[i];
		//init aligner
		Aligner aligner;
		aligner.doAlignment(agroups);
		mavenParameters->writeCSVFlag = true;
	}
}

void PeakDetector::processSlices(vector<mzSlice*>&slices, string setName) {

	if (slices.size() == 0)
		return;
	mavenParameters->allgroups.clear();
	sort(slices.begin(), slices.end(), mzSlice::compIntensity);

	//process KNOWNS
	QTime timer;
	timer.start();
	qDebug() << "Proessing slices: setName=" << setName.c_str() << " slices="
			<< slices.size();

	int converged = 0;
	int foundGroups = 0;

	int eicCount = 0;
	int groupCount = 0;
	int peakCount = 0;

	// for all the slies, find EICs and score quality and rank ofpeaks in the EICs
	// using the classifier
	for (unsigned int s = 0; s < slices.size(); s++) {
		mzSlice* slice = slices[s];
		double mzmin = slice->mzmin;
		double mzmax = slice->mzmax;
		double rtmin = slice->rtmin;
		double rtmax = slice->rtmax;

		Compound* compound = slice->compound;

		if (compound != NULL && compound->hasGroup())
			compound->unlinkGroup();

		if (mavenParameters->checkConvergance) {
			mavenParameters->allgroups.size() - foundGroups > 0 ? converged =
																			0 :
																	converged++;
			if (converged > 1000)
				break;	 //exit main loop
			foundGroups = mavenParameters->allgroups.size();
		}

		// for all the slies, find EICs
		vector<EIC*> eics = pullEICs(slice, mavenParameters->samples,
				EicLoader::PeakDetection, mavenParameters->eic_smoothingWindow,
				mavenParameters->eic_smoothingAlgorithm, mavenParameters->amuQ1,
				mavenParameters->amuQ3,
				mavenParameters->baseline_smoothingWindow,
				mavenParameters->baseline_dropTopX);

		float eicMaxIntensity = 0;

		for (unsigned int j = 0; j < eics.size(); j++) {
			eicCount++;
			if (eics[j]->maxIntensity > eicMaxIntensity)
				eicMaxIntensity = eics[j]->maxIntensity;
		}
		if (eicMaxIntensity < mavenParameters->minGroupIntensity) {
			delete_all(eics);
			continue;
		}

		//for ( unsigned int j=0; j < eics.size(); j++ )	eics[j]->getPeakPositions(eic_smoothingWindow);
		vector<PeakGroup> peakgroups = EIC::groupPeaks(eics,
				mavenParameters->eic_smoothingWindow,
				mavenParameters->grouping_maxRtWindow);

		//score quality of each group using classifier
		vector<PeakGroup*> groupsToAppend;
		for (int j = 0; j < peakgroups.size(); j++) {
			PeakGroup& group = peakgroups[j];
			group.computeAvgBlankArea(eics);
			group.groupStatistics();
			groupCount++;
			peakCount += group.peakCount();

			if (mavenParameters->clsf->hasModel()) {
				mavenParameters->clsf->classify(&group);
				group.groupStatistics();
			}
			if (mavenParameters->clsf->hasModel()
					&& group.goodPeakCount < mavenParameters->minGoodPeakCount)
				continue;
			if (mavenParameters->clsf->hasModel()
					&& group.maxQuality < mavenParameters->minQuality)
				continue;
			// if (group.blankMean*minBlankRatio > group.sampleMean ) continue;
			if (group.blankMax * mavenParameters->minSignalBlankRatio
					> group.maxIntensity)
				continue;
			if (group.maxNoNoiseObs < mavenParameters->minNoNoiseObs)
				continue;
			if (group.maxSignalBaselineRatio
					< mavenParameters->minSignalBaseLineRatio)
				continue;
			if (group.maxIntensity < mavenParameters->minGroupIntensity)
				continue;

			if (compound)
				group.compound = compound;
			if (!slice->srmId.empty())
				group.srmId = slice->srmId;

			//update peak group rank using rt difference b/w compound's expectedRt and peak groups's RT
			if (mavenParameters->matchRtFlag && compound != NULL
					&& compound->expectedRt > 0) {
				float rtDiff = abs(compound->expectedRt - (group.meanRt));
				group.expectedRtDiff = rtDiff;
				group.groupRank = rtDiff * rtDiff * (1.1 - group.maxQuality)
						* (1 / log(group.maxIntensity + 1)); //TODO Formula to rank groups
				if (group.expectedRtDiff > mavenParameters->compoundRTWindow)
					continue;
			} else {
				group.groupRank = (1.1 - group.maxQuality)
						* (1 / log(group.maxIntensity + 1));
			}

			groupsToAppend.push_back(&group);
		}

		//sort groups according to their rank
		std::sort(groupsToAppend.begin(), groupsToAppend.end(),
				PeakGroup::compRankPtr);

		for (int j = 0; j < groupsToAppend.size(); j++) {
			//check for duplicates	and append group
			if (j >= mavenParameters->eicMaxGroups)
				break;

			PeakGroup* group = groupsToAppend[j];
			bool ok = addPeakGroup(*group);

			//force insert when processing compounds.. even if duplicated
			if (ok == false && compound != NULL)
				mavenParameters->allgroups.push_back(*group);
		}

		//cleanup
		delete_all(eics);

		if (mavenParameters->allgroups.size()
				> mavenParameters->limitGroupCount) {
			break;
		}

		//		if (stopped())
		//		break;

		if (mavenParameters->showProgressFlag && s % 10 == 0) {

			QString progressText = "Found "
					+ QString::number(mavenParameters->allgroups.size())
					+ " groups";
			/*TODO: Fix emit update progress of slices.
			 emit(
			 updateProgressBar(progressText, s + 1,
			 std::min((int) slices.size(), limitGroupCount)));
			 */
		}
	}

	qDebug() << "processSlices() Slices=" << slices.size();
	qDebug() << "processSlices() EICs=" << eicCount;
	qDebug() << "processSlices() Groups=" << groupCount;
	qDebug() << "processSlices() Peaks=" << peakCount;
	qDebug() << "processSlices() done. " << timer.elapsed() << " sec.";
//cleanup();
}

bool PeakDetector::addPeakGroup(PeakGroup& grp1) {

	/**
	 * check overlap between RT for each group through all the samples
	 * if a certain degree of overlap is present, do not create a new group
	 */

	for (int i = 0; i < mavenParameters->allgroups.size(); i++) {
		PeakGroup& grp2 = mavenParameters->allgroups[i];
		float rtoverlap = mzUtils::checkOverlap(grp1.minRt, grp1.maxRt,
				grp2.minRt, grp2.maxRt);
		if (rtoverlap > 0.9
				&& ppmDist(grp2.meanMz, grp1.meanMz)
						< mavenParameters->ppmMerge) {
			return false;
		}
	}
	//ekse push the group to the allgroups vector
	mavenParameters->allgroups.push_back(grp1);
	return true;
}