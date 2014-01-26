#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <stdio.h>
#include <float.h>
#include "stock.h"
#include "elog.h"
#include "pf.h"
#include "seispp.h"
#include "Metadata.h"
#include "gclgrid.h"
#include "agc.h"
#include "vtk_output.h"

using namespace SEISPP;

int vtk_output_GCLgrid(GCLgrid& g, string ofile);
/* Removes mean for constant x3 slices.  Important for
proper display of tomography models showing absolute velocities */
void remove_mean_x3(GCLscalarfield3d& f)
{
	double mean;
	int i,j,k;
	double nval=static_cast<double>((f.n1)*(f.n2));
	cout << "Mean removal values"<<endl
		<< "grid-index     mean"<<endl;
	for(k=0;k<f.n3;++k)
	{
		mean=0.0;
		for(i=0;i<f.n1;++i)
			for(j=0;j<f.n2;++j)
				mean += f.val[i][j][k];
		mean /= nval;
		cout << k << "      "<<mean<<endl;
		for(i=0;i<f.n1;++i)
			for(j=0;j<f.n2;++j)
				f.val[i][j][k] = f.val[i][j][k]-mean;
	}
}
/* Applies an agc operator with window length iwagc to data along the x3 direction
then normalizes a vector constructed from each x3 grid line to have rms=1 (L2 norm unity).
*/
void agc_scalar_field(GCLscalarfield3d& g, int iwagc)
{
	vector<double> x3vals;
	double nrmx3v;
	int i,j,k;
	for(i=0;i<g.n1;++i)
		for(j=0;j<g.n2;++j)
		{
			x3vals.clear();
			for(k=0;k<g.n3;++k) x3vals.push_back(g.val[i][j][k]);
			x3vals=agc<double>(x3vals,iwagc);
			nrmx3v=dnrm2(g.n3,&(x3vals[0]),1);
			/* do nothing if nrm is 0 */
			if(nrmx3v<DBL_EPSILON) nrmx3v=1.0;
			for(k=0;k<g.n3;++k) g.val[i][j][k]=x3vals[k]/nrmx3v;
		}
}
		

void usage()
{
	cerr << "gclfield2vtk db|file outfile [-i -g gridname -f fieldname -r remapgridname "
		<< "-odbf outfieldname -xml -binary -pf pffile] -V" << endl;
	exit(-1);
}
bool SEISPP::SEISPP_verbose(true);
int main(int argc, char **argv)
{
	int i;

        bool dbmode(true);  // default is db input mode
        ios::sync_with_stdio();
	elog_init(argc,argv);
	if(argc<3) usage();
	string dbname(argv[1]);
        string infile(argv[1]);  // redundant but easier to do this way
	string outfile(argv[2]);
	string argstr;
	string pffile("gclfield2vtk");
	const string nodef("NOT DEFINED");
	string gridname(nodef);
	string fieldname(nodef);
	string remapgridname(nodef);
	bool saveagcfield(false);
	string outfieldname;
	bool remap(false);
	bool xmloutput(false);
	bool binaryout(false);
	for(i=3;i<argc;++i)
	{
		argstr=string(argv[i]);
		if(argstr=="-pf")
		{
			++i;
			if(i>=argc)usage();
			pffile=string(argv[i]);
		}
                else if(argstr=="-i")
                    dbmode=false;
		else if(argstr=="-V")
		{
		    cbanner("1.0",
			"gclfield2vtk db outfile [-pf pffile]",
			"Gary L. Pavlis",
			"Indiana University",
			"pavlis@indiana.edu");
		}
		else if(argstr=="-g")
		{
			++i;
			if(i>=argc)usage();
			gridname=string(argv[i]);
		}
		else if(argstr=="-f")
		{
			++i;
			if(i>=argc)usage();
			fieldname=string(argv[i]);
		}
		else if(argstr=="-r")
		{
			++i;
			if(i>=argc)usage();
			remapgridname=string(argv[i]);
			remap=true;
		}
		else if(argstr=="-odbf")
		{
			++i;
			if(i>=argc)usage();
			saveagcfield=true;
			outfieldname=string(argv[i]);
		}
		else if(argstr=="-xml")
		{
			xmloutput=true;
		}
		else if(argstr=="-binary")
		{
			binaryout=true;
		}
		else
		{
			usage();
		}
	}
        Pf *pf;
        if(pfread(const_cast<char *>(pffile.c_str()),&pf))
                elog_die(1,"pfread error\n");

	try {
        	Metadata control(pf);
                DatascopeHandle dbh;
                if(dbmode)
                    dbh=DatascopeHandle(dbname,true);

		if(gridname==nodef)
			gridname=control.get_string("gridname");
		if(fieldname==nodef)
			fieldname=control.get_string("fieldname");
		string fieldtype=control.get_string("fieldtype");
		cout << "Converting field = "<<fieldname
			<< " associated with gridname="<<gridname<<endl;
		cout << "Assuming grid type is "<<fieldtype<<endl;
                int nv_expected(5);  // defaulted for pwmig
                if(fieldtype=="vector3d")
                {
                    nv_expected=control.get_int("nv_expected");
                }
		/* Slightly odd logic here, but this allows remap off
		to be the default.  pf switch is ignored this way if
		the -r flag was used */
		if(!remap) remap=control.get_bool("remap_grid");
		BasicGCLgrid *rgptr;
		if(remap)
		{
                        if(!dbmode)
                        {
                            cerr << "Illegal argument combination"<<endl
                                << "remap option only available with db input"
                                <<endl;
                            exit(-1);
                        }
			if(remapgridname==nodef)
				remapgridname=control.get_string("remapgridname");
			cout << "Remapping emabled.  "
				<<"Will use coordinate system of grid "
				<< remapgridname<<endl;
			int griddim=control.get_int("remapgrid_number_dimensions");
			if(griddim==3)
			{
                                GCLgrid3d *gtmp;
                                 gtmp=new GCLgrid3d(dbh,
					remapgridname);
				rgptr=dynamic_cast<BasicGCLgrid*>(gtmp);
			}
			else if(griddim==2)
			{
                            GCLgrid *gtmp;
                            gtmp=new GCLgrid(dbh,
					remapgridname);
			    rgptr=dynamic_cast<BasicGCLgrid*>(gtmp);
			}
			else
			{
				cerr << "Illegal remap grid dimension="
					<<griddim<<endl
					<<"Fix parameter file"<<endl;
			}
		}
			
		bool SaveAsVectorField;
		int VectorComponent;
		bool rmeanx3=control.get_bool("remove_mean_x3_slices");
                if(rmeanx3)
                    cout << "remove mean for each slice enabled"<<endl;
		bool apply_agc=control.get_bool("apply_agc");
		int iwagc(0);
		if(apply_agc)
		{
			iwagc=control.get_int("agc_operator_length");
                        cout << "Applying agc operator with length="<<iwagc
                            << " depth intervals"<<endl;
		}
                else
                    cout << "Date being written with true amplitude"<<endl;
		if(saveagcfield  && !apply_agc)
		{
			cerr << "Illegal option combination"<<endl
			  << "-odbf argument can only be used if apply_agc"
			  << " pf parameter is set true"<<endl;
			exit(-1);
		}
		string fielddir;
		if(saveagcfield)
			fielddir=control.get_string("field_directory");
		if(fieldtype=="scalar3d") 
		{
                        GCLscalarfield3d field;
                        if(dbmode)
			    field=GCLscalarfield3d(dbh,gridname,fieldname);
                        else
                            field=GCLscalarfield3d(infile);
			if(remap)
			{
				// Used to make this optional.  force
				//if(field!=(*rgptr))
					remap_grid(dynamic_cast<GCLgrid3d&>(field),
						*rgptr);
			}
			if(rmeanx3) remove_mean_x3(field);
			if(apply_agc) agc_scalar_field(field,iwagc);
			output_gcl3d_to_vtksg(field,outfile,xmloutput,binaryout);
			if(saveagcfield) 
				field.save(dbh,string(""),fielddir,
				  outfieldname,outfieldname);
		}
		else if(fieldtype=="vector3d")
		{
			SaveAsVectorField=control.get_bool("save_as_vector_field");
			if(SaveAsVectorField)
			{
				cerr << "Vector field output not yet supported but planned"
					<< endl;
			}
			if(rmeanx3)
			{
				cerr << "remove_mean_x3_slices set true:  "
					<< "ignored for vector field="
					<< fieldname<<endl;
			}
                        GCLvectorfield3d vfield;
                        if(dbmode)
			    vfield=GCLvectorfield3d(dbh,gridname,
                                    fieldname,nv_expected);
                        else
                            vfield=GCLvectorfield3d(infile);
			GCLscalarfield3d *sfptr;
			if(remap)
			{
				//if(vfield!=(*rgptr))
					remap_grid(dynamic_cast<GCLgrid3d&>(vfield),
						*rgptr);
			}
			for(i=0;i<vfield.nv;++i)
			{
				sfptr = extract_component(vfield,i);
				if(apply_agc) agc_scalar_field(*sfptr,iwagc);
				stringstream ss;
				ss << outfile <<"_"<<i;
				if(xmloutput) 
					ss<<".vts";
				else
					ss<<".vtk";
				output_gcl3d_to_vtksg(*sfptr,
					ss.str(),xmloutput,binaryout);
				/*This is not ideal, but will do this now
				for expedience.  This creates a series of 
				scalar fields when saveagcfield is enabled
				that have a naming convention similar to
				the vtk files.  These could easily overflow
				but I don't test them here. beware */
				if(saveagcfield) 
				{
					stringstream ssof;
					ssof << outfieldname<<"_"<<i;
					string ofld=ssof.str();
					sfptr->save(dbh,string(""),
					  fielddir,ofld,ofld);
				}
				delete sfptr;
			}
		}
		else if(fieldtype=="grid2d")
		{
			int npoly;
                        GCLgrid g;
                        if(dbmode)
			    g=GCLgrid(dbh,gridname);
                        else
                            g=GCLgrid(infile);
			if(remap)
			{
				//if(g!=(*rgptr))
					remap_grid(g,*rgptr);
			}
			npoly=vtk_output_GCLgrid(g,outfile);
			cout << "Wrote "<<npoly<<" polygons to output file"<<endl;
			if(apply_agc)cerr <<"WARNING: apply_agc was set true\n"
					<<"This is ignored for grids and 2d fields"<<endl;
		}
		else if(fieldtype=="scalar2d")
		{
			int npoly;
                        GCLscalarfield field;
			if(dbmode)
                            field=GCLscalarfield(dbh,gridname,fieldname);
                        else 
                            field=GCLscalarfield(infile);
			if(remap)
			{
			    remap_grid(dynamic_cast<GCLgrid&>(field),
						*rgptr);
			}
			vtk_output_GCLgrid(dynamic_cast<GCLgrid&>(field),outfile);
			cout << "Wrote "<<npoly<<" polygons to output file"<<endl;
			if(apply_agc)cerr <<"WARNING: apply_agc was set true\n"
					<<"This is ignored for grids and 2d fields"<<endl;
		}	
		else
		{
			cerr << "Unsupported fieldtype = "<<fieldtype<<endl
				<< "Exiting with no output\n" << endl;
		}

	}
	catch (MetadataGetError& mderr)
	{
		mderr.log_error();
		exit(-1);
	}
        catch(GCLgridError& gerr)
        {
            cerr << "GCLgridError thrown"<<endl<<gerr.what();
        }
        catch(std::exception& sexcp)
        {
            cerr << sexcp.what();
        }
	catch (...)
	{
		elog_die(1,"Something threw an unhandled excepton\n");
	}
}