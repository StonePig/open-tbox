#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <json.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <errno.h>
#include <sys/statvfs.h>
#include "tl-logger.h"

#define TL_LOGGER_STORAGE_BASE_PATH_DEFAULT "/var/lib/tbox/log"

#define TL_LOGGER_LOG_ITEM_HEAD_MAGIC (const guint8 *)"TLIH"
#define TL_LOGGER_LOG_ITEM_TAIL_MAGIC (const guint8 *)"TLIT"
#define TL_LOGGER_LOG_SIZE_MAXIUM 8 * 1024 * 1024

#define TL_LOGGER_LOG_FREE_SPACE_MINIUM 200UL * 1024 * 1024
#define TL_LOGGER_LOG_FREE_NODE_MINIUM 2048

typedef struct _TLLoggerData
{
    gboolean initialized;
    gchar *storage_base_path;
    gint64 last_timestamp;
    gint64 new_timestamp;
    
    GMutex cached_log_mutex;
    GQueue *cached_log_data;
    GQueue *write_log_queue;
    GList *last_saved_data;
    GHashTable *last_log_data;
    guint log_update_timeout_id;
    
    GThread *write_thread;
    gboolean write_thread_work_flag;
    
    GThread *archive_thread;
    gboolean archive_thread_work_flag;
    gint archive_thread_wait_countdown;
    
    GThread *query_thread;
    gboolean query_thread_work_flag;
}TLLoggerData;

typedef struct _TLLoggerFileStat
{
    gchar *name;
    guint64 size;
}TLLoggerFileStat;

static TLLoggerData g_tl_logger_data = {0};

static TLLoggerLogItemData *tl_logger_log_item_data_dup(
    TLLoggerLogItemData *data)
{
    TLLoggerLogItemData *new_data;
    
    if(data==NULL)
    {
        return NULL;
    }
    
    new_data = g_new0(TLLoggerLogItemData, 1);
    new_data->name = g_strdup(data->name);
    new_data->value = data->value;
    new_data->unit = data->unit;
    new_data->source = data->source;
    
    return new_data;
}

static void tl_logger_log_item_data_free(TLLoggerLogItemData *data)
{
    if(data==NULL)
    {
        return;
    }
    if(data->name!=NULL)
    {
        g_free(data->name);
    }
    g_free(data);
}

static void tl_logger_file_stat_free(TLLoggerFileStat *data)
{
    if(data==NULL)
    {
        return;
    }
    if(data->name!=NULL)
    {
        g_free(data->name);
    }
    g_free(data);
}

static int tl_logger_file_stat_compare(const TLLoggerFileStat *a,
    const TLLoggerFileStat *b, gpointer user_data)
{
    if(a==NULL && b==NULL)
    {
        return 0;
    }
    else if(a==NULL)
    {
        return -1;
    }
    else if(b==NULL)
    {
        return 1;
    }
    return g_strcmp0(a->name, b->name);
}


static inline guint16 tl_logger_crc16_compute(const guchar *data_p,
    gsize length)
{
    guchar x;
    guint16 crc = 0xFFFF;
    while(length--)
    {
        x = crc >> 8 ^ *data_p++;
        x ^= x>>4;
        crc = (crc << 8) ^ ((guint16)(x << 12)) ^ ((guint16)(x <<5)) ^
            ((guint16)x);
    }
    return crc;
}


/*
 * Log Frame:
 * | Head Magic (4B) | Length (4B) | CRC16 (2B) | JSON Data | Tail Magic (4B) |
 * 
 */

static GByteArray *tl_logger_log_to_file_data(GHashTable *log_data)
{
    GByteArray *ba;
    GHashTableIter iter;
    TLLoggerLogItemData *item_data;
    json_object *root, *child, *item_object;
    const gchar *json_data;
    guint32 json_len, belen;
    guint16 crc, becrc;
    
    ba = g_byte_array_new();
    
    g_byte_array_append(ba, TL_LOGGER_LOG_ITEM_HEAD_MAGIC, 4);
    g_byte_array_append(ba, (const guint8 *)"\0\0\0\0", 4);
    g_byte_array_append(ba, (const guint8 *)"\0\0", 2);
    
    root = json_object_new_array();
    
    g_hash_table_iter_init(&iter, log_data);
    while(g_hash_table_iter_next(&iter, NULL, (gpointer *)&item_data))
    {
        item_object = json_object_new_object();
        
        child = json_object_new_string(item_data->name);
        json_object_object_add(item_object, "name", child);
        
        child = json_object_new_int64(item_data->value);
        json_object_object_add(item_object, "value", child);
        
        child = json_object_new_double(item_data->unit);
        json_object_object_add(item_object, "unit", child);
        
        child = json_object_new_int(item_data->source);
        json_object_object_add(item_object, "source", child);
        
        json_object_array_add(root, item_object);
    }
    
    json_data = json_object_to_json_string(root);
    json_len = strlen(json_data);
    g_byte_array_append(ba, (const guint8 *)json_data, json_len);
    
    crc = tl_logger_crc16_compute((const guchar *)json_data, json_len);
    
    json_object_put(root);
    
    belen = g_htonl(json_len);
    memcpy(ba->data+4, &belen, 4);
    
    becrc = g_htons(crc);
    memcpy(ba->data+8, &becrc, 2);
    
    g_byte_array_append(ba, TL_LOGGER_LOG_ITEM_TAIL_MAGIC, 4);
    
    return ba;
}

static void tl_logger_archives_clear_old(TLLoggerData *logger_data,
    guint64 freespace, guint64 freeinodes)
{
    GDir *log_dir;
    GError *error = NULL;
    const gchar *filename;
    struct stat statbuf;
    TLLoggerFileStat *file_stat;
    GSequence *file_sequence;
    GSequenceIter *iter;
    gchar *fullpath;
    
    log_dir = g_dir_open(logger_data->storage_base_path, 0, &error);
    if(error!=NULL)
    {
        g_warning("TLLogger cannot open log storage directionary: %s",
            error->message);
        return;
    }
    
    file_sequence = g_sequence_new((GDestroyNotify)tl_logger_file_stat_free);
    
    while((filename=g_dir_read_name(log_dir))!=NULL)
    {
        if(g_str_has_suffix(filename, ".tlz"))
        {
            fullpath = g_build_filename(logger_data->storage_base_path,
                filename, NULL);
            if(stat(fullpath, &statbuf)==0)
            {
                file_stat = g_new0(TLLoggerFileStat, 1);
                file_stat->name = fullpath;
                file_stat->size = statbuf.st_size;
                
                g_sequence_insert_sorted(file_sequence, file_stat, 
                    (GCompareDataFunc)tl_logger_file_stat_compare,
                    logger_data);
            }
            else
            {
                g_warning("TLLogger cannot stat archived log file %s: %s",
                    filename, strerror(errno));
                g_free(fullpath);
            }
            
        }
    }
    
    for(iter=g_sequence_get_begin_iter(file_sequence);
        !g_sequence_iter_is_end(iter);iter=g_sequence_iter_next(iter))
    {
        file_stat = g_sequence_get(iter);
        if(file_stat==NULL)
        {
            continue;
        }
        
        if(freespace > TL_LOGGER_LOG_FREE_SPACE_MINIUM &&
            freeinodes > TL_LOGGER_LOG_FREE_NODE_MINIUM)
        {
            break;
        }
        
        if(g_remove(file_stat->name)==0)
        {
            freespace += file_stat->size;
            freeinodes++;
        }
        else
        {
            g_warning("TLLogger failed to remove old archive file %s: %s",
                file_stat->name, strerror(errno));
            break;
        }
    }
    
    g_sequence_free(file_sequence);
    
    g_dir_close(log_dir);
}

static gboolean tl_logger_log_archive_compress_file(TLLoggerData *logger_data,
    const gchar *file)
{
    gboolean ret = TRUE;
    GZlibCompressor *compressor;
    GFileOutputStream *file_ostream;
    GOutputStream *compress_ostream;
    GFile *output_file;
    gssize write_size;
    GError *error = NULL;
    FILE *fp;
    gchar *tmpname;
    gchar buff[4096];
    size_t rsize;
    gchar *newname;
    
    if(file==NULL)
    {
        return FALSE;
    }
    
    tmpname = g_build_filename(logger_data->storage_base_path, "tlz.tmp",
        NULL);
    output_file = g_file_new_for_path(tmpname);
    if(output_file==NULL)
    {
        g_free(tmpname);
        return FALSE;
    }
    
    file_ostream = g_file_replace(output_file, NULL, FALSE,
        G_FILE_CREATE_PRIVATE, NULL, &error);
    g_object_unref(output_file);
    
    if(file_ostream==NULL)
    {
        g_warning("TLLogger cannot open output file stream: %s",
            error->message);
        g_clear_error(&error);
        g_free(tmpname);
        return FALSE;
    }
    
    compressor = g_zlib_compressor_new(G_ZLIB_COMPRESSOR_FORMAT_ZLIB, 5);
    compress_ostream = g_converter_output_stream_new(G_OUTPUT_STREAM(
        file_ostream), G_CONVERTER(compressor));
    g_object_unref(file_ostream);
    g_object_unref(compressor);
    
    fp = fopen(file, "r");
    if(fp==NULL)
    {
        g_warning("TLLogger failed to open origin log file for new "
            "archived log: %s", strerror(errno));
        g_output_stream_close(compress_ostream, NULL, NULL);
        g_object_unref(compress_ostream);
        return FALSE;
    }
    
    while((rsize=fread(buff, 4096, 1, fp))>0)
    {
        write_size += g_output_stream_write(compress_ostream, buff,
            rsize, NULL, &error);
        if(error!=NULL)
        {
            g_warning("TLLogger cannot write archive: %s", error->message);
            g_clear_error(&error);
            ret = FALSE;
            break;
        }
    }
    fclose(fp);

    g_output_stream_close(compress_ostream, NULL, &error);
    if(error!=NULL)
    {
        g_warning("TLLogger cannot close archive file stream: %s",
            error->message);
        g_clear_error(&error);
        ret = FALSE;
    }
    g_object_unref(compress_ostream);

    if(ret)
    {
        newname = g_strdup_printf("%sz", file);
        g_rename(tmpname, newname);
        g_free(newname);
    }
    
    g_free(tmpname);

    return ret;
}

static gpointer tl_logger_log_query_thread(gpointer user_data)
{
    TLLoggerData *logger_data = (TLLoggerData *)user_data;
    
    if(user_data==NULL)
    {
        return NULL;
    }
    
    logger_data->query_thread_work_flag = TRUE;
    
    return NULL;
}

static gpointer tl_logger_log_archive_thread(gpointer user_data)
{
    TLLoggerData *logger_data = (TLLoggerData *)user_data;
    GDir *log_dir;
    GError *error = NULL;
    const gchar *filename;
    gchar *fullpath;
    struct statvfs statbuf;
    guint64 freespace;
    guint64 freeinodes;
    
    if(user_data==NULL)
    {
        return NULL;
    }
    
    log_dir = g_dir_open(logger_data->storage_base_path, 0, &error);
    if(error!=NULL)
    {
        g_warning("TLLogger cannot open log storage directionary: %s",
            error->message);
        return NULL;
    }
    
    logger_data->archive_thread_work_flag = TRUE;
    
    while(logger_data->archive_thread_work_flag)
    {
        if(logger_data->archive_thread_wait_countdown>0)
        {
            logger_data->archive_thread_wait_countdown--;
            g_usleep(1000000);
        }
        else /* Scan unarchived log every 60s. */
        {
            if(statvfs(logger_data->storage_base_path, &statbuf)==0)
            {
                freespace = (guint64)statbuf.f_bavail * statbuf.f_bsize;
                freeinodes = statbuf.f_favail;
                
                if(freespace < TL_LOGGER_LOG_FREE_SPACE_MINIUM ||
                    freeinodes < TL_LOGGER_LOG_FREE_NODE_MINIUM)
                {
                    /* Clear the disk for more space or inodes */
                    tl_logger_archives_clear_old(logger_data, freespace,
                        freeinodes);
                }
            }
            else
            {
                g_warning("TLLogger cannot stat storage directory: %s",
                    strerror(errno));
            }
            
            while((filename=g_dir_read_name(log_dir))!=NULL)
            {
                if(g_str_has_suffix(filename, ".tl"))
                {
                    fullpath = g_build_filename(
                        logger_data->storage_base_path, filename, NULL);
                    tl_logger_log_archive_compress_file(logger_data,
                        fullpath);
                    g_free(fullpath);
                }
            }
            
            g_dir_rewind(log_dir);
            
            logger_data->archive_thread_wait_countdown = 60;
        }
    }
    
    g_dir_close(log_dir);
    
    return NULL;
}

static gpointer tl_logger_log_write_thread(gpointer user_data)
{
    TLLoggerData *logger_data = (TLLoggerData *)user_data;
    GHashTable *item_data;
    GByteArray *ba;
    int fd = -1;
    ssize_t written_size = 0, rsize;
    gchar *lastlog_filename = NULL;
    gchar *datestr, *filename, *fullpath;
    gchar *lastlog_basename = NULL;
    gint64 last_write_time = G_MININT64, write_time;
    TLLoggerLogItemData *time_item;
    GDateTime *dt;
    
    if(user_data==NULL)
    {
        return NULL;
    }
    
    logger_data->write_thread_work_flag = TRUE;
    
    while(logger_data->write_thread_work_flag)
    {
        g_mutex_lock(&(logger_data->cached_log_mutex));
        
        logger_data->last_saved_data = g_queue_pop_head_link(
            logger_data->write_log_queue);
        
        if(logger_data->last_saved_data==NULL)
        {
            g_usleep(100000);
            g_mutex_unlock(&(logger_data->cached_log_mutex));
            continue;
        }
        
        item_data = logger_data->last_saved_data->data;
        if(item_data==NULL)
        {
            g_list_free_full(logger_data->last_saved_data,
                (GDestroyNotify)tl_logger_log_item_data_free);
            logger_data->last_saved_data = NULL;
            
            g_usleep(100000);
            g_mutex_unlock(&(logger_data->cached_log_mutex));
            continue;
        }
        
        time_item = g_hash_table_lookup(item_data, "time");
        if(time_item==NULL)
        {
            g_list_free_full(logger_data->last_saved_data,
                (GDestroyNotify)tl_logger_log_item_data_free);
            logger_data->last_saved_data = NULL;
            
            g_usleep(100000);
            g_mutex_unlock(&(logger_data->cached_log_mutex));
            continue;
        }
        
        write_time = time_item->value;
        
        if(fd>=0 && write_time < last_write_time)
        {
            /* Log time is not monotonic! */
            fsync(fd);
            close(fd);
            fd = -1;
            
            if(lastlog_filename!=NULL && lastlog_basename!=NULL)
            {
                filename = g_strdup_printf("%s.tl", lastlog_basename);
                fullpath = g_build_filename(
                    logger_data->storage_base_path, filename, NULL);
                g_free(filename);
                
                g_rename(lastlog_basename, fullpath);
                
                g_free(fullpath);
            }
            
            g_queue_free_full(logger_data->cached_log_data,
                (GDestroyNotify)g_hash_table_unref);
            logger_data->cached_log_data = g_queue_new();
            logger_data->last_saved_data = NULL;
            
            logger_data->archive_thread_wait_countdown = 0;
        }
        last_write_time = write_time;
        
        ba = tl_logger_log_to_file_data(item_data);
        
        g_queue_push_tail_link(logger_data->cached_log_data,
            logger_data->last_saved_data);
        
        g_mutex_unlock(&(logger_data->cached_log_mutex));
        
        if(fd<0)
        {
            if(lastlog_filename!=NULL)
            {
                g_free(lastlog_filename);
            }
            if(lastlog_basename!=NULL)
            {
                g_free(lastlog_basename);
            }
            dt = g_date_time_new_from_unix_local(write_time);
            datestr = g_date_time_format(dt, "%Y%m%d%H%M%S");
            lastlog_basename = g_strdup_printf("tbl-%s", datestr);
            filename = g_strdup_printf("%s.tlw", lastlog_basename);
            g_free(datestr);
            g_date_time_unref(dt);
            
            lastlog_filename = g_build_filename(
                logger_data->storage_base_path, filename, NULL);
            g_free(filename);
            
            fd = open(lastlog_filename, O_WRONLY | O_CREAT);
            written_size = 0;
        }
        
        rsize = write(fd, ba->data, ba->len);
        g_byte_array_unref(ba);
        fsync(fd);
        
        if(rsize>0)
        {
            written_size += rsize;
        }
        
        if(rsize<=0 || written_size>=TL_LOGGER_LOG_SIZE_MAXIUM)
        {
            fsync(fd);
            close(fd);
            fd = -1;
            
            if(lastlog_filename!=NULL && lastlog_basename!=NULL)
            {
                filename = g_strdup_printf("%s.tl", lastlog_basename);
                fullpath = g_build_filename(
                    logger_data->storage_base_path, filename, NULL);
                g_free(filename);
                
                g_rename(lastlog_basename, fullpath);
                
                g_free(fullpath);
            }
            
            g_mutex_lock(&(logger_data->cached_log_mutex));
            g_queue_free_full(logger_data->cached_log_data,
                (GDestroyNotify)g_hash_table_unref);
            logger_data->cached_log_data = g_queue_new();
            logger_data->last_saved_data = NULL;
            g_mutex_unlock(&(logger_data->cached_log_mutex));
            
            logger_data->archive_thread_wait_countdown = 0;
        }
        
    }
    
    if(lastlog_filename!=NULL)
    {
        g_free(lastlog_filename);
    }
    
    return NULL;
}

static gboolean tl_logger_log_update_timer_cb(gpointer user_data)
{
    TLLoggerData *logger_data = (TLLoggerData *)user_data;
    TLLoggerLogItemData *item_data, *dup_data;
    GHashTableIter iter;
    GDateTime *dt;
    GHashTable *dup_table;
    
    if(logger_data->new_timestamp > logger_data->last_timestamp +
        (gint64)10000000)
    {
        dt = g_date_time_new_now_local();
        dup_table = g_hash_table_new_full(g_str_hash,
            g_str_equal, NULL, (GDestroyNotify)tl_logger_log_item_data_free);
        item_data = g_new0(TLLoggerLogItemData, 1);
        item_data->name = g_strdup("time");
        item_data->value = g_date_time_to_unix(dt);
        g_date_time_unref(dt);
        item_data->unit = 1.0;
        item_data->source = 0;
        g_hash_table_replace(dup_table, item_data->name, item_data);
        
        g_hash_table_iter_init(&iter, logger_data->last_log_data);
        while(g_hash_table_iter_next(&iter, NULL, (gpointer *)&item_data))
        {
            if(item_data==NULL)
            {
                continue;
            }
            dup_data = tl_logger_log_item_data_dup(item_data);
            g_hash_table_replace(dup_table, dup_data->name, dup_data);
        }
        
        g_mutex_lock(&(logger_data->cached_log_mutex));
        g_queue_push_tail(logger_data->write_log_queue, dup_data);
        g_mutex_unlock(&(logger_data->cached_log_mutex));
                
        logger_data->last_timestamp = logger_data->new_timestamp;
    }
    
    return TRUE;
}

gboolean tl_logger_init(const gchar *storage_base_path)
{
    GDir *log_dir;
    GError *error = NULL;
    const gchar *filename;
    gchar *fullpath, *newpath;
    size_t slen;
    
    if(g_tl_logger_data.initialized)
    {
        g_warning("TLLogger already initialized!");
        return TRUE;
    }
    
    g_mutex_init(&(g_tl_logger_data.cached_log_mutex));
    g_tl_logger_data.cached_log_data = g_queue_new();
    g_tl_logger_data.write_log_queue = g_queue_new();
    
    if(storage_base_path!=NULL)
    {
        g_tl_logger_data.storage_base_path = g_strdup(storage_base_path);
    }
    else
    {
        g_tl_logger_data.storage_base_path = g_strdup(
            TL_LOGGER_STORAGE_BASE_PATH_DEFAULT);
    }
    
    log_dir = g_dir_open(g_tl_logger_data.storage_base_path, 0, &error);
    if(error!=NULL)
    {
        g_warning("TLLogger cannot open log storage directionary: %s",
            error->message);
        g_free(g_tl_logger_data.storage_base_path);
        g_tl_logger_data.storage_base_path = NULL;
        return FALSE;
    }
    
    while((filename=g_dir_read_name(log_dir))!=NULL)
    {
        if(g_str_has_suffix(filename, ".tlw"))
        {
            fullpath = g_build_filename(g_tl_logger_data.storage_base_path,
                filename, NULL);
            newpath = g_strdup(fullpath);
            slen = strlen(newpath);
            newpath[slen-1] = '\0';
            
            g_rename(fullpath, newpath);
            
            g_free(newpath);
            g_free(fullpath);
        }
    }
    
    g_dir_close(log_dir);
    
    g_tl_logger_data.log_update_timeout_id = g_timeout_add_seconds(10,
        tl_logger_log_update_timer_cb, &g_tl_logger_data);
    
    g_tl_logger_data.write_thread = g_thread_new("tl-logger-write-thread",
        tl_logger_log_write_thread, &g_tl_logger_data);
        
    g_tl_logger_data.archive_thread = g_thread_new("tl-logger-archive-thread",
        tl_logger_log_archive_thread, &g_tl_logger_data);
        
    g_tl_logger_data.query_thread = g_thread_new("tl-logger-query-thread",
        tl_logger_log_query_thread, &g_tl_logger_data);
    
    g_tl_logger_data.initialized = TRUE;
    
    return TRUE;
}

void tl_logger_uninit()
{
    if(!g_tl_logger_data.initialized)
    {
        return;
    }
    
    if(g_tl_logger_data.log_update_timeout_id>0)
    {
        g_source_remove(g_tl_logger_data.log_update_timeout_id);
        g_tl_logger_data.log_update_timeout_id = 0;
    }
    
    if(g_tl_logger_data.query_thread!=NULL)
    {
        g_tl_logger_data.query_thread_work_flag = FALSE;
        g_thread_join(g_tl_logger_data.query_thread);
        g_tl_logger_data.query_thread = NULL;
    }
    
    if(g_tl_logger_data.write_thread!=NULL)
    {
        g_tl_logger_data.write_thread_work_flag = FALSE;
        g_thread_join(g_tl_logger_data.write_thread);
        g_tl_logger_data.write_thread = NULL;
    }
    
    if(g_tl_logger_data.archive_thread!=NULL)
    {
        g_tl_logger_data.archive_thread_work_flag = FALSE;
        g_tl_logger_data.archive_thread_wait_countdown = 0;
        g_thread_join(g_tl_logger_data.archive_thread);
        g_tl_logger_data.archive_thread = NULL;
    }

    if(g_tl_logger_data.last_log_data!=NULL)
    {
        g_hash_table_unref(g_tl_logger_data.last_log_data);
        g_tl_logger_data.last_log_data = NULL;
    }
    
    if(g_tl_logger_data.write_log_queue!=NULL)
    {
        g_queue_free_full(g_tl_logger_data.write_log_queue,
            (GDestroyNotify)g_hash_table_unref);
        g_tl_logger_data.write_log_queue = NULL;
    }
    if(g_tl_logger_data.cached_log_data!=NULL)
    {
        g_queue_free_full(g_tl_logger_data.cached_log_data,
            (GDestroyNotify)g_hash_table_unref);
        g_tl_logger_data.cached_log_data = NULL;
    }
    
    if(g_tl_logger_data.storage_base_path!=NULL)
    {
        g_free(g_tl_logger_data.storage_base_path);
        g_tl_logger_data.storage_base_path = NULL;
    }
    
    g_mutex_clear(&(g_tl_logger_data.cached_log_mutex));
    
    g_tl_logger_data.initialized = FALSE;
}

void tl_logger_current_data_update(const TLLoggerLogItemData *item_data)
{
    TLLoggerLogItemData *idata;
    
    if(item_data==NULL || item_data->name==NULL)
    {
        return;
    }
    if(g_tl_logger_data.last_log_data==NULL)
    {
        g_tl_logger_data.last_log_data = g_hash_table_new_full(g_str_hash,
            g_str_equal, NULL, (GDestroyNotify)tl_logger_log_item_data_free);
    }
    if(g_hash_table_contains(g_tl_logger_data.last_log_data, item_data->name))
    {
        idata = g_hash_table_lookup(g_tl_logger_data.last_log_data,
            item_data->name);
        if(idata!=NULL)
        {
            idata->value = item_data->value;
            idata->unit = item_data->unit;
            idata->source = item_data->source;
        }
    }
    else
    {
        idata = g_new(TLLoggerLogItemData, 1);
        idata->name = g_strdup(item_data->name);
        idata->value = item_data->value;
        idata->unit = item_data->unit;
        idata->source = item_data->source;
        g_hash_table_replace(g_tl_logger_data.last_log_data, idata->name,
            idata);
    }
    
    g_tl_logger_data.new_timestamp = g_get_monotonic_time();
}

GHashTable *tl_logger_current_data_get()
{
    return g_tl_logger_data.last_log_data;
}
