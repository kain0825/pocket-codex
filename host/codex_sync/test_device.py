import io,json,tempfile,time,unittest,wave
from pathlib import Path
from device_api import VoiceJobs,VOICE_MAX_FRAMES,VOICE_MAX_BYTES,VOICE_MAX_TEXT_CHARS

def wav(rate=16000,frames=8000):
    b=io.BytesIO()
    with wave.open(b,'wb') as w:w.setnchannels(1);w.setsampwidth(2);w.setframerate(rate);w.writeframes(b'\0\0'*frames)
    return b.getvalue()

class Tests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.jobs=VoiceJobs(Path(self.tmp.name),lambda path:'测试内容')
        self.ident='a'*32
    def tearDown(self):self.jobs.executor.shutdown(wait=True);self.tmp.cleanup()
    def test_wav_transcription_and_separate_files(self):
        raw=wav();self.jobs.submit(self.ident,raw)
        self.jobs.executor.shutdown(wait=True)
        self.assertEqual(self.jobs.read(self.ident)['state'],'ready')
        self.assertEqual((Path(self.tmp.name)/self.ident/'original.wav').read_bytes(),raw)
        self.assertEqual((Path(self.tmp.name)/self.ident/'transcript.txt').read_text(encoding='utf-8'),'测试内容')
        self.assertEqual(self.jobs.submit(self.ident,raw)['state'],'ready')
        with self.assertRaises(ValueError):self.jobs.submit(self.ident,wav(frames=9000))
    def test_invalid_audio_rejected(self):
        for raw in (wav(8000),wav(frames=100),wav()[:-20]):
            with self.assertRaises(ValueError):self.jobs.submit(self.ident,raw)
        self.assertFalse((Path(self.tmp.name)/self.ident).exists())
        with self.assertRaises(ValueError):self.jobs.read('../../invalid')
    def test_restart_marks_unfinished_failed(self):
        path=Path(self.tmp.name)/self.ident;path.mkdir();(path/'job.json').write_text(json.dumps({'state':'transcribing'}))
        other=VoiceJobs(Path(self.tmp.name),lambda path:'test')
        try:self.assertEqual(other.read(self.ident)['state'],'failed')
        finally:other.executor.shutdown()
    def test_edit_preserves_transcript_and_audio(self):
        raw=wav();self.jobs.submit(self.ident,raw);self.jobs.executor.shutdown(wait=True)
        result=self.jobs.draft(self.ident,'修改后的草稿')
        self.assertEqual(result['text'],'测试内容')
        self.assertEqual(result['editedText'],'修改后的草稿')
        self.assertEqual((Path(self.tmp.name)/self.ident/'original.wav').read_bytes(),raw)
        self.assertEqual((Path(self.tmp.name)/self.ident/'transcript.txt').read_text(encoding='utf-8'),'测试内容')
        self.assertEqual(self.jobs.draft(self.ident,'')['state'],'empty')
    def test_manual_draft_without_recording(self):
        result=self.jobs.draft(self.ident,'手动输入')
        self.assertEqual(result['state'],'ready')
        self.assertEqual(result['editedText'],'手动输入')
        self.assertFalse((Path(self.tmp.name)/self.ident/'original.wav').exists())

    def test_recording_beyond_one_minute_is_accepted_intact(self):
        raw=wav(frames=16000*75)
        self.jobs.submit(self.ident,raw);self.jobs.executor.shutdown(wait=True)
        saved=Path(self.tmp.name)/self.ident/'original.wav'
        self.assertEqual(saved.read_bytes(),raw)
        with wave.open(str(saved),'rb') as w:self.assertEqual(w.getnframes(),16000*75)
        self.assertEqual(self.jobs.read(self.ident)['state'],'ready')

    def test_exact_ten_minute_limit_and_one_sample_over(self):
        raw=wav(frames=VOICE_MAX_FRAMES)
        self.assertEqual(len(raw),VOICE_MAX_BYTES)
        self.jobs.submit(self.ident,raw);self.jobs.executor.shutdown(wait=True)
        self.assertEqual(self.jobs.read(self.ident)['state'],'ready')
        with self.assertRaises(ValueError):self.jobs.submit('b'*32,wav(frames=VOICE_MAX_FRAMES+1))
        self.assertFalse((Path(self.tmp.name)/('b'*32)).exists())

    def test_long_transcript_is_not_silently_truncated(self):
        text='完整转写'*1500
        self.jobs.transcriber=lambda path:text
        self.jobs.submit(self.ident,wav());self.jobs.executor.shutdown(wait=True)
        self.assertEqual(self.jobs.read(self.ident)['text'],text)
        self.assertEqual((Path(self.tmp.name)/self.ident/'transcript.txt').read_text('utf-8'),text)

    def test_overlong_transcript_is_preserved_and_not_a_sendable_prefix(self):
        text='字'*(VOICE_MAX_TEXT_CHARS+1)
        self.jobs.transcriber=lambda path:text
        self.jobs.submit(self.ident,wav());self.jobs.executor.shutdown(wait=True)
        result=self.jobs.read(self.ident)
        self.assertEqual(result['state'],'too_long')
        self.assertEqual(result['text'],'')
        self.assertEqual((Path(self.tmp.name)/self.ident/'transcript.txt').read_text('utf-8'),text)

    def test_extended_editor_limit_matches_sendable_text(self):
        self.assertEqual(self.jobs.draft(self.ident,'字'*VOICE_MAX_TEXT_CHARS)['state'],'ready')
        with self.assertRaises(ValueError):self.jobs.draft(self.ident,'字'*(VOICE_MAX_TEXT_CHARS+1))

    def test_traditional_recognition_returns_simplified_draft_and_keeps_raw(self):
        original='這個語音草稿，請幫我錄音並轉寫。Codex v73 16kHz，11/06，🙂'
        expected='这个语音草稿，请帮我录音并转写。Codex v73 16kHz，11/06，🙂'
        self.jobs.transcriber=lambda path:original
        raw=wav();self.jobs.submit(self.ident,raw);self.jobs.executor.shutdown(wait=True)
        folder=Path(self.tmp.name)/self.ident;result=self.jobs.read(self.ident)
        self.assertEqual(result['state'],'ready')
        self.assertEqual(result['text'],expected)
        self.assertEqual(result['textScript'],'Hans')
        self.assertEqual((folder/'transcript.txt').read_text('utf-8'),original)
        self.assertEqual((folder/'normalized-transcript.txt').read_text('utf-8'),expected)
        self.assertEqual((folder/'original.wav').read_bytes(),raw)
        self.jobs.draft(self.ident,'我刻意保留「繁體」和 API_Name。')
        self.assertEqual(self.jobs.read(self.ident)['editedText'],'我刻意保留「繁體」和 API_Name。')
        self.assertEqual((folder/'transcript.txt').read_text('utf-8'),original)

    def test_simplified_and_non_chinese_text_are_not_rewritten(self):
        from speech_text import simplified_transcript
        for text in ['', '已经是简体中文，保留标点。', 'Codex API_v3 123 https://example.com/a?b=1 🙂']:
            self.assertEqual(simplified_transcript(text),text)
        self.assertEqual(simplified_transcript('頭髮與發送，乾坤與乾杯。'),'头发与发送，乾坤与干杯。')

if __name__=='__main__':unittest.main()
