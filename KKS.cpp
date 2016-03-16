// KKS.cpp
// Algorithms for 2D and 3D isotropic binary alloy solidification
// Questions/comments to trevor.keller@nist.gov (Trevor Keller)

#ifndef KKS_UPDATE
#define KKS_UPDATE
#include<cmath>
#include"MMSP.hpp"
#include"KKS.hpp"

// Note: KKS.hpp contains important declarations and comments. Have a look.

namespace MMSP{

double h(const double& p)     {return p;                        }
double hprime(const double& p){return 1.0;                      }
double g(const double& p)     {return pow(p,2.0)*pow(1.0-p,2.0);}
double gprime(const double& p){return p*(2.0*p-1.0)*(p-1.0);    }

const double fA = 1.0;    // equilibrium free energy of pure A
const double fB = 2.5;    // equilibrium free energy of pure B
const double R = 8.314e-3;// J/cm3-K
const double T = 373.15;  // Isothermal.

double fl(const double& c)
{
	// ideal solution model for liquid free energy
	const double cA = 1.0-c;
	const double cB = c;
	const double tiny = 1.0e-6;

	if (cA < tiny)
		return fB;
	else if (cB < tiny)
		return fA;
	return cA*fA + cB*fB + R*T*(cA*log(cA) + cB*log(cB));
}

double fs(const double& c)
{
	// ideal solution model for solid free energy, transformed from liquid
	double delta = -0.25; // negative solidifies, positive melts
	return fl(1.0-c) + delta;
}

double k()
{
	// Partition coefficient, from solving dfs_dc = 0 and dfl_dc = 0
	double Cs_e = std::exp((fB-fA)/(R*T)) / (1.0 + std::exp((fB-fA)/(R*T)));
	double Cl_e = std::exp((fA-fB)/(R*T)) / (1.0 + std::exp((fA-fB)/(R*T)));

	return Cs_e / Cl_e;
}

double dfl_dc(const double& c)
{
	const double cA = 1.0-c;
	const double cB = c;
	const double tiny = 1.0e-6;

	if (std::min(cA,cB) < tiny)
		return fB-fA;
	return fB - fA + R*T*(log(cB) - log(cA));
}

double dfs_dc(const double& c)
{
	return 2.0*(fA - fB) + dfl_dc(1.0-c);
}

double d2fl_dc2(const double& c)
{
	const double cA = 1.0-c;
	const double cB = c;
	return R*T/(cA*cB);
}

double d2fs_dc2(const double& c)
{
	return d2fl_dc2(1.0-c);
}

double f(const double& p, const double&c, const double& Cs, const double& Cl)
{
	const double w = 0.5; // well barrier height
	return w*g(p) + h(p)*fs(Cs) + (1.0-h(p))*fl(Cl);
}

double d2f_dc2(const double& p, const double&c, const double& Cs, const double& Cl)
{
	double R = h(p)*d2fl_dc2(Cl) + (1.0-h(p))*d2fs_dc2(Cs);
	return d2fl_dc2(Cl)*d2fs_dc2(Cs)/R;
}

void generate(int dim, const char* filename)
{
	/* Grid contains four fields:
	 * 0. phi, phase fraction solid. Phi=1 means Solid.
	 * 1. c, concentration of component A
	 * 2. Cs, fictitious composition of solid
	 * 3. Cl, fictitious composition of liquid
	 */
	const double cBs = 0.45; // initial solid concentration
	const double cBl = 0.65; // initial liquid concentration

	unsigned int nSol=0, nLiq=0;
	if (dim==1) {
		int L=1024;
		GRID1D initGrid(4,0,L);

		for (int n=0; n<nodes(initGrid); n++) {
			vector<int> x = position(initGrid,n);
			double r = 32-x[0]%64;
			if (r<16.0) { // Solid
				nSol++;
				initGrid(n)[0] = 1.0;
				initGrid(n)[1] = cBs;
			} else {
				nLiq++;
				initGrid(n)[0] = 0.0;
				initGrid(n)[1] = cBl;
			}
		}
		output(initGrid,filename);
	} else if (dim==2) {
		int L=256;
		GRID2D initGrid(4,0,2*L,0,L);

		for (int n=0; n<nodes(initGrid); n++) {
			vector<int> x = position(initGrid,n);
			double r = sqrt(pow(32-x[0]%64,2)+pow(32-x[1]%64,2));
			if (r<16.0) { // Solid
				nSol++;
				initGrid(n)[0] = 1.0;
				initGrid(n)[1] = cBs;
			} else {
				nLiq++;
				initGrid(n)[0] = 0.0;
				initGrid(n)[1] = cBl;
			}
		}
		output(initGrid,filename);
	} else if (dim==3) {
		int L=64;
		GRID3D initGrid(4,0,2*L,0,L,0,L/4);

		for (int n=0; n<nodes(initGrid); n++) {
			vector<int> x = position(initGrid,n);
			double r = sqrt(pow(32-x[0]%64,2)+pow(32-x[1]%64,2));
			if (r<16.0) { // Solid
				nSol++;
				initGrid(n)[0] = 1.0;
				initGrid(n)[1] = cBs;
			} else {
				nLiq++;
				initGrid(n)[0] = 0.0;
				initGrid(n)[1] = cBl;
			}
		}
		output(initGrid,filename);
	} else {
		std::cerr<<"ERROR: "<<dim<<"-dimensional domains not supported."<<std::endl
		MMSP::Abort(-1);
	}
	unsigned int nTot = nSol+nLiq;
	std::cout<<"System is "<<(100*nSol)/nTot<<"% solid, "<<(100*nLiq)/nTot<<"% liquid."<<std::endl;

	/* Generate Cs,Cl look-up table (LUT) using Newton-Raphson method, outlined in Provatas' Appendix C3
	 * Store results in pureconc, which contains two fields:
	 * 0. Cl, fictitious composition of pure liquid
	 * 1. Cs, fictitious composition of pure solid
	 *
	 * The grid is discretized over phi (axis 0) and c (axis 1).
	*/
	double C0 = (double(nLiq)*cBs + double(nSol)*cBl) / double(nSol+nLiq); // weighted average of solid + liquid
	std::assert(C0>0.);
	int LUTres[2] = {100, 100};
	LUTGRID pureconc(2,0,LUTres[0],0,LUTres[1]);
	double dp = 1.0/LUTres[0];
	double dc = 1.0/LUTres[1];
	dx(pureconc,0) = dp; // different resolution in phi
	dx(pureconc,1) = dc; // and c is not unreasonable

	for (int n=0; n<nodes(pureconc); n++) {
		vector<int> x = position(pureconc,n);
		iterateConc(dp*double(x[0]), dc*double(x[1]), pureconc[0], pureconc[1]);
	}
	output(pureconc,"consistentC.lut");
}

template <int dim, typename T> void update(grid<dim,vector<T> >& oldGrid, int steps)
{
	int rank=0;
    #ifdef MPI_VERSION
    rank = MPI::COMM_WORLD.Get_rank();
    #endif

	ghostswap(oldGrid);

   	grid<dim,vector<T> > newGrid(oldGrid);

	double dt = 0.01;

	for (int step=0; step<steps; step++) {
		if (rank==0)
			print_progress(step, steps);

		for (int i=0; i<nodes(oldGrid); i++) {
			const vector<T>& phi = oldGrid(n);

			// compute laplacian
			vector<T> lap = laplacian(oldGrid,n);

			// compute sum of squares
			T sum = 0.0;
			for (int j=0; j<fields(oldGrid); j++) {
				sum += phi[j]*phi[j];
			}

			// compute update values
			for (int j=0; j<fields(oldGrid); j++) {
				newGrid(n)[j] = phi[j] - dt*(-phi[j]-pow(phi[j],3)+2.0*(phi[j]*sum-lap[j]));
			}
		}
		swap(oldGrid,newGrid);
		ghostswap(oldGrid);
	}
}

/* Given const phase fraction (p) and concentration (c), iteratively determine
 * the solid (Cs) and liquid (Cl) fictitious concentrations that satisfy the
 * equal chemical potential constraint. Pass p and c by const value,
 * Cs and Cl by non-const reference to update in place. This allows use ofthis
 * single function to both populate the LUT and interpolate values based thereupon.
 */
template<typename T,typename U> void iterateConc(const T p, const T c, U& Cs, U& Cl)
{
	
}


} // namespace MMSP

#endif

#include"MMSP.main.hpp"
