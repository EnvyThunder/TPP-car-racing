#ifndef GL1REDIRECT_H
#define GL1REDIRECT_H

// Points the game's OpenGL 1 / GLU calls at gl1shim (OpenGL ES 2 / WebGL underneath). Include it
// after all other headers in the files that draw (glwidget*.cpp, carmodel.cpp): from here on
// glBegin(...) means sgl::begin(...), and so on.

#include "gl1shim.h"

#define glBegin sgl::begin
#define glEnd sgl::end
#define glVertex2f sgl::vertex2f
#define glVertex3f sgl::vertex3f
#define glColor3f sgl::color3f
#define glColor4f sgl::color4f
#define glColor3fv sgl::color3fv
#define glColor4fv sgl::color4fv
#define glNormal3f sgl::normal3f
#define glTexCoord2f sgl::texCoord2f

#define glMatrixMode sgl::matrixMode
#define glLoadIdentity sgl::loadIdentity
#define glPushMatrix sgl::pushMatrix
#define glPopMatrix sgl::popMatrix
#define glTranslatef sgl::translatef
#define glRotatef sgl::rotatef
#define glScalef sgl::scalef
#define glOrtho sgl::ortho
#define gluPerspective sgl::perspective
#define gluLookAt sgl::lookAt
#define glGetFloatv sgl::getFloatv

#define glEnable sgl::enable
#define glDisable sgl::disable
#define glBlendFunc sgl::blendFunc
#define glDepthMask sgl::depthMask
#define glDepthFunc sgl::depthFunc
#define glViewport sgl::viewport
#define glClear sgl::clear
#define glClearColor sgl::clearColor
#define glLineWidth sgl::lineWidth
#define glPointSize sgl::pointSize
#define glShadeModel sgl::shadeModel
#define glHint sgl::hint
#define glColorMaterial sgl::colorMaterial
#define glLightfv sgl::lightfv
#define glLightModelfv sgl::lightModelfv
#define glMaterialfv sgl::materialfv
#define glMaterialf sgl::materialf
#define glFogi sgl::fogi
#define glFogf sgl::fogf
#define glFogfv sgl::fogfv
#define glTexEnvi sgl::texEnvi
#define glTexGeni sgl::texGeni
#define glPushAttrib sgl::pushAttrib
#define glPopAttrib sgl::popAttrib

#define glGenTextures sgl::genTextures
#define glDeleteTextures sgl::deleteTextures
#define glBindTexture sgl::bindTexture
#define glTexParameteri sgl::texParameteri
#define glTexParameterf sgl::texParameterf
#define glPixelStorei sgl::pixelStorei
#define glTexImage2D sgl::texImage2D
#define gluBuild2DMipmaps sgl::build2DMipmaps
#define glGetString sgl::getString

#define glGenLists sgl::genLists
#define glDeleteLists sgl::deleteLists
#define glNewList sgl::newList
#define glEndList sgl::endList
#define glCallList sgl::callList

#define gluNewQuadric sgl::newQuadric
#define gluDeleteQuadric sgl::deleteQuadric
#define gluQuadricNormals sgl::quadricNormals
#define gluCylinder sgl::cylinder
#define gluDisk sgl::disk
#define gluSphere sgl::sphere

#endif // GL1REDIRECT_H
