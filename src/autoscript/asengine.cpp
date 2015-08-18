#include "asengine.h"
#include "asmodules.h"

#include <dronehelper.h>

#include "opencv/asmodules_opencv.h"
#include <string>
#include <boost/filesystem.hpp>
#include <boost/python/stl_iterator.hpp>
#include "asioredirector.h"

using namespace std;
namespace py = boost::python;

BOOST_PYTHON_MODULE(autoscript)
{
	py::class_<AutoScriptModule>("AutoScriptControl")
				.def("takeoff", &AutoScriptModule::takeoff)
				.def("land", &AutoScriptModule::land)
                .def("move", &AutoScriptModule::move)
				.def("move_rel", &AutoScriptModule::move_rel)
                .def("hover", &AutoScriptModule::hover)
                .def("flip", &AutoScriptModule::flip)
                .def("navdata", &AutoScriptModule::navdata)
                .def("status", &AutoScriptModule::status)
                .def("flattrim", &AutoScriptModule::flattrim)
			    .def("set_view", &AutoScriptModule::set_view)
                .def("startrecording", &AutoScriptModule::startrecording)
                .def("stoprecording", &AutoScriptModule::stoprecording)
                .def("switchview", &AutoScriptModule::switchview)
                .def("takepicture", &AutoScriptModule::takepicture);

	py::class_<ImgProc>("ImgProc")
				.def("latest_frame", &ImgProc::getLatestFrame)
                .def("frame_age", &ImgProc::getFrameAge)
				.def("show_frame", &ImgProc::showFrame)
                .def("start_tag_detector", &ImgProc::startTagDetector)
                .def("stop_tag_detector", &ImgProc::stopTagDetector)
                .def("set_tag_family", &ImgProc::setTagFamily)
                .def("set_tag_roi", &ImgProc::setTagROI)
                .def("tag_detections", &ImgProc::getTagDetections);
}

BOOST_PYTHON_MODULE(autoscriptioredirector)
{
	py::class_<ASIORedirector>("autoscriptioredirector", py::init<>())
			    .def("write", &ASIORedirector::write);
}

ASEngine::ASEngine(shared_ptr<FPVDrone> drone)
{
	_drone = drone;
}

void ASEngine::initPython()
{
	if(Py_IsInitialized() == 0)
	{
		PyImport_AppendInittab("autoscript", &PyInit_autoscript);
		PyImport_AppendInittab("autoscriptioredirector", &PyInit_autoscriptioredirector);
		Py_SetProgramName((wchar_t *)"AutoFlight");
		Py_Initialize();
        PyEval_InitThreads();
        PyEval_SaveThread();
	}
}

ASEngine::~ASEngine()
{

}

shared_ptr<FPVDrone> ASEngine::drone()
{
	return _drone;
}

vector<string> ASEngine::getAvailableFunctions()
{
    // TODO: do not hardcode these

	vector<string> funcs = {
            "basicctl.flattrim",
            "basicctl.flip",
            "basicctl.hover",
            "basicctl.land",
            "basicctl.move",
            "basicctl.move_rel",
            "basicctl.navdata",
			"basicctl.set_view",
            "basicctl.startrecording",
            "basicctl.status",
            "basicctl.stoprecording",
			"basicctl.switchview",
            "basicctl.takeoff",
            "basicctl.takepicture",
            "imgproc.frame_age",
            "imgproc.latest_frame",
            "imgproc.set_tag_family",
            "imgproc.set_tag_roi",
            "imgproc.show_frame",
            "imgproc.start_tag_detector",
            "imgproc.stop_tag_detector",
            "imgproc.tag_detections"
	};

	return funcs;
}

string ASEngine::getPythonVersion()
{
	string v = Py_GetVersion();

	return v.substr(0, v.find_first_of(' '));
}

bool ASEngine::runScript(string script, bool simulate, IScriptSimulationUI *ssui, ImageVisualizer *iv, ASError *e, function<void(const string &)> outputCallback)
{
	if(simulate && ssui == NULL)
	{
		// Make sure nothing is trying to simulate without providing a IScriptSimulationUI
		return false;
	}

    PyGILState_STATE state = PyGILState_Ensure();

    PyEval_ReInitThreads();

    _asmodule = new AutoScriptModule(_drone, simulate, ssui);
    _imgproc = new ImgProc(_drone, iv, simulate, ssui);

	bool initialized = false;
	bool error = false;

    // Initialize namespaces and modules
    py::object main_module = py::import("__main__");
    py::object main_namespace = main_module.attr("__dict__");

    // Set up the standard output redirector
    py::object redirector_module((py::handle<>(PyImport_ImportModule("autoscriptioredirector"))));
    main_namespace["autoscriptioredirector"] = redirector_module;

    ASIORedirector redirector;
    redirector.addOutputListener(outputCallback);
    boost::python::import("sys").attr("stderr") = redirector;
    boost::python::import("sys").attr("stdout") = redirector;

    // Import drone control functions
    py::object autoscript_module((py::handle<>(PyImport_ImportModule("autoscript"))));
    main_namespace["autoscript"] = autoscript_module;

    main_namespace["basicctl"] = py::ptr(_asmodule);
    main_namespace["imgproc"] = py::ptr(_imgproc);

	try
    {
		initialized = true;

		py::exec(py::str(script), main_namespace);

		drone_hover(_drone);

		error = false;
	}
	catch(const py::error_already_set &ex)
	{
		drone_hover(_drone);

		if(e != NULL)
		{
			*e = getLatestExceptionMessage();

			if(!initialized)
			{
				e->internalError = true;
			}
			else
			{
				e->internalError = false;
			}
		}
		else
		{
			cout << getLatestExceptionMessage().message << endl;
		}

		error = true;
	}

    _imgproc->stopTagDetector();

	delete _imgproc;
    delete _asmodule;
	_imgproc = nullptr;
    _asmodule = nullptr;

	PyGILState_Release(state);

	return !error;
}

int pyQuit(void *) // Gets injected into the script by stopRunningScript
{
	PyErr_SetInterrupt();
    return -1;
}

void ASEngine::stopRunningScript()
{
	if(_imgproc != nullptr)
	{
		_imgproc->abortFlag = true;
	}

	PyGILState_STATE state = PyGILState_Ensure();
	Py_AddPendingCall(&pyQuit, NULL);
	PyGILState_Release(state);
}

ASError ASEngine::getLatestExceptionMessage()
{
	ASError e;
	e.linenumber = -1;

	stringstream ss;

	if(PyErr_Occurred())
	{
		PyObject *ptype, *pvalue, *ptraceback;
		PyErr_Fetch(&ptype, &pvalue, &ptraceback);
		PyErr_NormalizeException(&ptype, &pvalue, &ptraceback);

		if(ptype != NULL)
		{
			py::object type(py::handle<>(py::allow_null(ptype)));
			py::object value(py::handle<>(py::allow_null(pvalue)));

			string strErrorMessage = "No error message available";

			if(pvalue != NULL)
			{
				strErrorMessage = py::extract<string>(PyObject_Str(pvalue));
			}

			if(ptraceback != NULL)
			{
				py::object traceback(py::handle<>(py::allow_null(ptraceback)));

				long lineno = py::extract<long>(traceback.attr("tb_lineno"));
				string filename = py::extract<string>(traceback.attr("tb_frame").attr("f_code").attr("co_filename"));
				string funcname = py::extract<string>(traceback.attr("tb_frame").attr("f_code").attr("co_name"));

				e.linenumber = lineno;
				e.filename = filename;
				e.funcname = funcname;

				ss << "Exception occurred in line " << lineno << " (" << funcname << ", " << filename << "):\n" << strErrorMessage << endl;
			}
			else
			{
				ss << "Exception occured:\n" << strErrorMessage << endl;
			}
		}
	}

	e.message = ss.str();

	return e;
}
