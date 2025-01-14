
/* Copyright (c) Mark J. Kilgard, 1994. */

/* This program is freely distributable without licensing fees 
   and is provided without guarantee or warrantee expressed or 
   implied. This program is -not- in the public domain. */

#ifdef __sgi
#include <malloc.h>
#endif
#include <stdlib.h>
#include <stdio.h>
#ifdef GLUT_WIN32
#include <windows.h>
#endif
#include <GL/glut.h>
#include <gltint.h>

int main_w, w1, w2, w3, w4;

void
display(void)
{
  glClear(GL_COLOR_BUFFER_BIT);
  glutSwapBuffers();
}

void
time8(int value)
{
  printf("PASS: test9\n");
  exit(0);
}

void
time7(int value)
{
  glutDestroyWindow(main_w);
  glutTimerFunc(500, time8, 0);
}

void
time6(int value)
{
  glutDestroyWindow(w1);
  glutTimerFunc(500, time7, 0);
  glutInitDisplayMode(GLUT_INDEX);
  w1 = glutCreateSubWindow(main_w, 10, 10, 10, 10);
  w2 = glutCreateSubWindow(w1, 10, 10, 30, 30);
  w3 = glutCreateSubWindow(w2, 10, 10, 50, 50);
  glutInitDisplayMode(GLUT_RGB);
  w4 = glutCreateSubWindow(w3, 10, 10, 70, 70);
  glClearColor(1.0, 1.0, 1.0, 1.0);
  glutDisplayFunc(display);
}

void
time5(int value)
{
  w1 = glutCreateSubWindow(main_w, 10, 10, 10, 10);
  w2 = glutCreateSubWindow(w1, 10, 10, 30, 30);
  w3 = glutCreateSubWindow(w2, 10, 10, 50, 50);
  glutInitDisplayMode(GLUT_RGB);
  w4 = glutCreateSubWindow(w3, 10, 10, 70, 70);
  glClearColor(1.0, 1.0, 1.0, 1.0);
  glutDisplayFunc(display);
  glutTimerFunc(500, time6, 0);
}

void
time4(int value)
{
  glutDestroyWindow(w4);
  glutTimerFunc(500, time5, 0);
}

void
time3(int value)
{
  glutDestroyWindow(w3);
  glutTimerFunc(500, time4, 0);
}

void
time2(int value)
{
  glutDestroyWindow(w2);
  glutTimerFunc(500, time3, 0);
}

void
time1(int value)
{
  glutDestroyWindow(w1);
  glutTimerFunc(500, time2, 0);
}

void
main(int argc, char **argv)
{
#if defined(__sgi) && !defined(REDWOOD)
  /* XXX IRIX 6.0.1 mallopt(M_DEBUG, 1) busted. */
  mallopt(M_DEBUG, 1);
#endif
  glutInit(&argc, argv);

  glutInitDisplayMode(GLUT_RGB);
  main_w = glutCreateWindow("test9");
  glClearColor(0.0, 0.0, 0.0, 0.0);  /* black */
  glutDisplayFunc(display);
  glutInitDisplayMode(GLUT_INDEX);
  w1 = glutCreateSubWindow(main_w, 10, 10, 10, 10);
  glutSetColor(1, 1.0, 0.0, 0.0);  /* red */
  glutSetColor(2, 0.0, 1.0, 0.0);  /* green */
  glutSetColor(3, 0.0, 0.0, 1.0);  /* blue */
  glClearIndex(1);
  glutDisplayFunc(display);
  w2 = glutCreateSubWindow(main_w, 30, 30, 10, 10);
  glutCopyColormap(w1);
  glClearIndex(2);
  glutDisplayFunc(display);
  w3 = glutCreateSubWindow(main_w, 50, 50, 10, 10);
  glutCopyColormap(w1);
  glClearIndex(3);
  glutDisplayFunc(display);
  w4 = glutCreateSubWindow(main_w, 70, 70, 10, 10);
  glutCopyColormap(w1);
  glutSetColor(3, 1.0, 1.0, 1.0);  /* white */
  glClearIndex(3);
  glutDisplayFunc(display);
  glutTimerFunc(750, time1, 0);
  glutMainLoop();
}
