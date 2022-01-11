#ifndef FILE_SYNCHRONIZER_H
#define FILE_SYNCHRONIZER_H
/**
 * Copyright Whist Technologies, Inc.
 * @file file_synchronizer.h
 * @brief This file contains code to synchronizer file transfer between
 *        the client and server
============================
Usage
============================
init_file_synchronizer(FILE_TRANSFER_SERVER_DROP | FILE_TRANSFER_SERVER_UPLOAD);

FileChunk received_file_chunk;

// Write this received chunk to file
file_synchronizer_write_file_chunk(&received_file_chunk);

FileChunk* our_chunk_to_send;
file_synchronizer_read_next_file_chunk(0, &our_chunk_to_send);

if (our_chunk_to_send) {
  // We have a new file chunk, this should be sent to the server
  Send(our_chunk_to_send);
} else {
  // There is no new file chunk
}

destroy_file_synchronizer();
*/

/*
============================
Custom types
============================
*/

#define NUM_TRANSFERRING_FILES 5

/**
 * @brief                          The type of the file chunk being sent
 */
typedef enum FileChunkType {
    FILE_BODY,
    FILE_CLOSE,
    FILE_CLOSE_ACK  // Acknowledgement of closure by the write end
} FileChunkType;

/**
 * @brief                          A packet of data referring to and containing
 *                                 the information of a file chunk
 */
typedef struct FileData {
    int index;                 // The index of the file in the synchrony array
    size_t size;               // Number of bytes for the file chunk data
    FileChunkType chunk_type;  // Whether this is a first, middle or last chunk
    char data[0];              // The file chunk byte contents
} FileData;

/**
 * @brief                          The type of the file transfer, as a boolean enum
 */
typedef enum FileTransferType {
    FILE_TRANSFER_SERVER_DROP = 0b0001,     // Drop a file onto the server
    FILE_TRANSFER_CLIENT_DROP = 0b0010,     // Drop a file onto the client
    FILE_TRANSFER_SERVER_UPLOAD = 0b0100,   // Upload a file to the server
    FILE_TRANSFER_CLIENT_DOWNLOAD = 0b1000  // Download a file to the client
} FileTransferType;

/**
 * @brief                           Extra information for the file transfer
 */
typedef union FileEventInfo {
    struct {        // Info for FILE_TRANSFER_SERVER_DROP
        int32_t x;  // x-coordinate of file drop
        int32_t y;  // y-coordinate of file drop
    } server_drop;
} FileEventInfo;

/**
 * @brief                          A packet of data containing a file's metadata
 */
typedef struct FileMetadata {
    int index;                       // The index of the file in the synchrony array
    FileTransferType transfer_type;  // Type of file transfer
    FileEventInfo event_info;        // Extra information for the file transfer
    int file_size;                   // Total file size
    size_t filename_len;             // Length of the filename
    char filename[0];                // The file name
} FileMetadata;

/**
 * @brief                          The local direction of the transfer
 */
typedef enum FileTransferDirection {
    FILE_UNUSED,    // If file is not being read or written
    FILE_READ_END,  // If file is being read
    FILE_WRITE_END  // If file is being written
} FileTransferDirection;

/**
 * @brief                          A struct containing all the information for a
 *                                 a transferring file.
 */
typedef struct TransferringFile {
    int id;             // Unique identifier (unique across ALL written files,
                        //     not just active ones, but can be -1 for read files)
    char* filename;     // The filename without the path (can be NULL for read-end files)
    char* file_path;    // The local file path
    FILE* file_handle;  // The local file handle
    FileTransferType transfer_type;   // Type of file transfer
    FileEventInfo event_info;         // Extra information for the file transfer
    FileTransferDirection direction;  // FILE_READ_END if read end, FILE_WRITE_END if write end
} TransferringFile;

/*
============================
Public Functions
============================
*/

/**
 * @brief                          Initialize the file sychrony variables
 *
 * @param requested_actions        The file transfer types to attempt to initialize
 */
void init_file_synchronizer(FileTransferType requested_actions);

/**
 * @brief                          Open a file for writing based on `file_metadata`
 *
 * @param file_metadata            Pointer to file metadata
 *
 */
void file_synchronizer_open_file_for_writing(FileMetadata* file_metadata);

/**
 * @brief                          Write a file chunk to a transferring file
 *
 * @param file_chunk               The file data chunk to update the
 *                                 relevant transferring file with
 *
 * @returns                        true on success, false on failure
 */
bool file_synchronizer_write_file_chunk(FileData* file_chunk);

/**
 * @brief                          Set the basic file information in the file
 *                                 synchrony array for a file to be read. This
 *                                 function must be called for a specific file
 *                                 before the file is opened and metadata is read
 *                                 in file_synchronizer_open_file_for_reading.
 *
 * @param file_path                The local path of the file
 *
 * @param transfer_type            The type of the file transfer
 *
 * @param event_info               A pointer to any event-specific info that may
 *                                 be needed for the file transfer. For example,
 *                                 for FILE_TRANSFER_SERVER_DROP, this would
 *                                 consist of the x and y coordinate of the
 *                                 file drop.
 *
 */
void file_synchronizer_set_file_reading_basic_metadata(char* file_path,
                                                       FileTransferType transfer_type,
                                                       FileEventInfo* event_info);

/**
 * @brief                          Open a file and generate the Open a file
 *                                 for reading and generate the FileMetadata
 *
 * @param file_index               Index of file in file synchrony array
 *
 * @param file_metadata_ptr        Pointer to pointer for filled file metadata
 *
 */
void file_synchronizer_open_file_for_reading(int file_index, FileMetadata** file_metadata_ptr);

/**
 * @brief                          Read the next file chunk from a
 *                                 transferring file.
 *
 * @param file_index               Index of transferring file
 *
 * @param file_chunk_ptr           Pointer to pointer for filled file chunk
 *
 */
void file_synchronizer_read_next_file_chunk(int file_index, FileData** file_chunk_ptr);

/**
 * @brief                          Reset all transferring files
 */
void reset_all_transferring_files();

/**
 * @brief                          Cleanup the file synchronizer
 */
void destroy_file_synchronizer();

#endif  // FILE_SYNCHRONIZER_H
