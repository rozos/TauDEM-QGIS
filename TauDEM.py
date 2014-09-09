# -*- coding: utf-8 -*-
"""
/***************************************************************************
 TauDEM    
                                 A QGIS function
 Use this function to automate TauDEM commands run from within QGIS
                              -------------------
        begin                : 2014-09-09
        copyright            : (C) 2014 by ITIA
        email                : rozos@itia.ntua.gr
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/
"""
import subprocess
import os

_path=""
_dem=""
_taudem=""


def initialize(taudemPath, projectPath, DEMname):
    """Initialize the global variables of TauDEM module."""
    global _path, _dem, _taudem
    _path   = projectPath
    _dem    = DEMname
    _taudem = taudemPath


def autoDelineate(thresh, outlets=None):
    """Run all TauDEM commands to delineate a watershed."""
    if _path=="" or _dem=="" or _taudem=="":
        return "Please run initialize() first!"

    res = pitremove()
    if res != 0:
        return "pitremove failed with " + str(res)
    res = d8flowdir()
    if res != 0:
        return "d8flowdir failed with " + str(res)
    res = dinfflowdir()
    if res != 0:
        return "dinfflowdir failed with " + str(res)
    res = aread8()
    if res != 0:
        return "aread8 failed with " + str(res)
    res = areadinf()
    if res != 0:
        return "areadinf failed with " + str(res)
    res = gridnet()
    if res != 0:
        return "gridnet failed with " + str(res)
    res = peukerdouglas()
    if res != 0:
        return "peukerdouglas failed with " + str(res)
    res = aread8_outlets(outlets)
    if res != 0:
        return "aread8_outlets failed with " + str(res)
    if outlets!=None: res = dropanalysis(outlets)
    if res != 0:
        return "dropanalysis failed with " + str(res)
    res = threshold(thresh)
    if res != 0:
        return "threshold failed with " + str(res)
    res = streamnet(outlets)
    if res != 0:
        return "streamnet failed with " + str(res)

    return "OK!"


def argument(arg, suffix=None, ext="tif", basename=None):
    if suffix==None: 
        suffix=arg
    if basename==None:
        pathdem =os.path.join(_path, _dem ) 
    else:
        pathdem =os.path.join(_path, basename ) 
    return " -" + arg + " " + pathdem + suffix + "."+ext


def outletsarg(outlets):
    if outlets!=None:
        pathout =os.path.join(_path, outlets) 
        return  " -o " + pathout + ".shp"
    else:
        return ""


def reportError(cmd):
    errlogFile = os.path.join(_path, "error.log") 
    try:
        res = os.system(_taudem + cmd + " 1> " + errlogFile + " 2>&1")
        f=open(errlogFile, 'a+')
        f.write('\n\n THE PREVIOUS OUTPUT WAS PRODUCED BY THE FOLLOWING \n')
        f.write(_taudem + cmd +'\n')
    except Exception as e:
        res = str(e)
    return res


def pitremove():
    cmd = "pitremove" + argument("z", "") + argument("fel")
    res = os.system(os.path.join(_taudem,cmd))
    if res!=0:
        res=reportError(cmd)
    return res


def d8flowdir():
    cmd=  "d8flowdir" + argument("fel") + argument("p") + argument("sd8")
    res = os.system(os.path.join(_taudem,cmd))
    if res!=0:
        res=reportError(cmd)
    return res


def dinfflowdir():
    cmd= "dinfflowdir" + argument("fel") + argument("ang") + argument("slp")
    res = os.system(os.path.join(_taudem,cmd))
    if res!=0:
        res=reportError(cmd)
    return res


def aread8():
    cmd = "aread8" + argument("p") + argument("ad8")
    res = os.system(os.path.join(_taudem,cmd))
    if res!=0:
        res=reportError(cmd)
    return res


def aread8_outlets(outlets):
    cmd =  "aread8" + outletsarg(outlets) + argument("p")  \
                     + argument("wg","ss") + argument("ad8", "ssa")
    res = os.system(os.path.join(_taudem,cmd))
    if res!=0:
        res=reportError(cmd)
    return res


def areadinf():
    cmd = "areadinf" + argument("ang") + argument("sca")
    res = os.system(os.path.join(_taudem,cmd))
    if res!=0:
        res=reportError(cmd)
    return res


def gridnet():
    cmd = "gridnet" + argument("p") + argument("plen") \
                     + argument("tlen") + argument("gord")
    res = os.system(os.path.join(_taudem,cmd))
    if res!=0:
        res=reportError(cmd)
    return res


def peukerdouglas():
    cmd = "peukerdouglas" + argument("fel") + argument("ss")
    res = os.system(os.path.join(_taudem,cmd))
    if res!=0:
        res=reportError(cmd)
    return res


def dropanalysis(outlets):
    cmd = "dropanalysis" + outletsarg(outlets) + argument("p") \
                          + argument("fel") + argument("ssa") + argument("ad8")\
                          + argument("drp","drp","txt")
    res = os.system(os.path.join(_taudem,cmd))
    if res!=0:
        res=reportError(cmd)
    return res


def threshold(thresh):
    cmd = "threshold"+argument("ssa")+argument("src")+" -thresh " +str(thresh)
    res = os.system(os.path.join(_taudem,cmd))
    if res!=0:
        res=reportError(cmd)
    return res


def streamnet(outlets):
    cmd = "streamnet" + argument("fel") + argument("p") + argument("ad8") \
                        + argument("src") + argument("ord") \
                        + argument("tree", "tree", "dat") \
                        + argument("coord", "coord", "dat")  \
                        + outletsarg(outlets) \
                        + argument("net", "", "shp","River") + argument("w")
    res = os.system(os.path.join(_taudem,cmd))
    if res!=0:
        res=reportError(cmd)
    return res
