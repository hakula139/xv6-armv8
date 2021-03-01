#ifndef INC_LOG_H_
#define INC_LOG_H_

struct buf;

void initlog(int);
void log_write(struct buf*);
void begin_op();
void end_op();

#endif  // INC_LOG_H_
