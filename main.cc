#include <stdlib.h> // random() etc.
#include <string.h> // strlen() etc.
#include <stdio.h>  // printf() etc.
#include <time.h>   // time()
#include <math.h>   // fabsf()

#include <unistd.h> // fcntl()
#include <fcntl.h>

#include <GL/glut.h>

#define WIDTH 640
#define HEIGHT 640

unsigned char pixmap[HEIGHT][WIDTH];

void glWindowPos2iINT( GLint x, GLint y ) {
	glPushAttrib( GL_TRANSFORM_BIT | GL_VIEWPORT_BIT );
	glMatrixMode( GL_MODELVIEW  ); glPushMatrix(); glLoadIdentity();
	glMatrixMode( GL_PROJECTION ); glPushMatrix(); glLoadIdentity();
	glViewport( x-1, y-1, 2, 2 );
	glRasterPos2i( 0, 0 );
	glPopMatrix(); glMatrixMode( GL_MODELVIEW ); glPopMatrix();
	glPopAttrib();
}


void display() {

  // clear buffers
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  // move to origin
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();

  glWindowPos2iINT(0,HEIGHT-1);

  glPixelZoom(1.0,-1.0);

  glDrawPixels(WIDTH,HEIGHT,GL_LUMINANCE,GL_UNSIGNED_BYTE,pixmap);

  // redraw
  glutSwapBuffers();
}


void resize(int w, int h) {

  // set a whole-window viewport
  glViewport(0,0,w,h);

  // make a perspective projection matrix
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  //gluOrtho2D(-1.0,+1.0,-1.0,+1.0,1.0,500.0);

  // invalidate display
  glutPostRedisplay();
}


void keyboard(unsigned char key, int x, int y) {
  switch (key) {
    case 'q': exit(0); break;
  }
}

int last = 0;
int frame = 0;

void idle() {

	char buffer[10240];
	//printf("%c[2J\n%c[H\n",27,27);

	int count = read(0,buffer,sizeof(buffer));
	if (count <= 0) return;

	frame++;
	int curr = glutGet(GLUT_ELAPSED_TIME);
	int diff = curr - last;
	if (diff > 1000) {
		printf("frames: %d\n",frame);
		frame = 0;
		last = curr;
	}

	if (buffer[1] != 0x1C) {
		printf("unknown message\n");
		return;
	}

	for (int x = 0; x < WIDTH; x++) for (int y = 0; y < HEIGHT; y++) pixmap[y][x] = 0;

	int i = 2;

	while (i < count) {

		unsigned char y_  = buffer[i+0];
		unsigned char x1_ = buffer[i+1];
		unsigned char x2_ = buffer[i+2];
		unsigned char in_ = buffer[i+3];

		int y = y_; int x1 = x1_; int x2 = x2_; int in = in_;
		//printf("record: %x %x %x %x\n",y,x1,x2,in);

		// in is a bitfield:
		// 0
		// 1
		// 2 = ? \
		// 3 = ? |- 1 = "next blob" marker
		// 4 = ? /
		// 5 = msb of y
		// 6 = msb of x1
		// 7 = msb of x2

		i += 4;

		// empty record = end of packet
		if (!y && !x1 && !x2 && !in) break;

		// weird superficial "next block" marker?
		if (in & 0x1F) continue;

		// adjust MSB
		if (in & 0x20)  y += 255; 
		if (in & 0x40) x2 += 255; 
		if (in & 0x80) x1 += 255; 

		// adjust offset
		y  -= 11;
		x1 -= 41;
		x2 -= 41;

		// safety check
		if ((y <   0) || (x1 <   0) || (x2 <   0) ||
		    (y > 289) || (x1 > 356) || (x2 > 356)) {
			printf("out of range\n");
			continue;
		}

		// draw scanline
		for (int l = x1; l < x2; l++) pixmap[y][l] = 255;
	}

	glutPostRedisplay();
}



// initialize the GLUT library
void initGLUT(int *argcp, char **argv) {

  glutInitWindowSize(WIDTH,HEIGHT);
  glutInit(argcp,argv);
  glutInitDisplayMode(GLUT_RGBA | GLUT_DOUBLE | GLUT_DEPTH );
  glutCreateWindow("optitrack");
}


// initialize the GL library
void initGL() {

  // enable and set colors
  glEnable(GL_COLOR_MATERIAL);
  glClearColor(1.0,0.0,0.0,1.0);

  // enable and set depth parameters
  glDisable(GL_DEPTH_TEST);

  // antialiasing
  glDisable(GL_BLEND);

	glPixelStorei( GL_PACK_ALIGNMENT,   1 );
	glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );

   // misc stuff
  glDisable(GL_LIGHTING);
  glDisable(GL_CULL_FACE);
}



int main(int argc, char* argv[]) {

  // unblock stdin
  /*flags = fcntl(STDIN_FILENO,F_GETFL);
  flags |= O_NONBLOCK;
  fcntl(STDIN_FILENO,F_SETFL,flags);*/

  // lots of other init stuff
  initGLUT(&argc,argv);
  initGL();
  
  // make functions known to GLUT
  glutKeyboardFunc(keyboard);
  glutDisplayFunc(display);
  glutReshapeFunc(resize);
  glutIdleFunc(idle);

  // start the action
  glutMainLoop();
  
  return 0;
}

